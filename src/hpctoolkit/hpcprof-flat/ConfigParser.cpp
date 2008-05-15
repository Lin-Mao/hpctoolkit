// -*-Mode: C++;-*-
// $Id$

// * BeginRiceCopyright *****************************************************
// 
// Copyright ((c)) 2002-2007, Rice University 
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
// 
// * Redistributions of source code must retain the above copyright
//   notice, this list of conditions and the following disclaimer.
// 
// * Redistributions in binary form must reproduce the above copyright
//   notice, this list of conditions and the following disclaimer in the
//   documentation and/or other materials provided with the distribution.
// 
// * Neither the name of Rice University (RICE) nor the names of its
//   contributors may be used to endorse or promote products derived from
//   this software without specific prior written permission.
// 
// This software is provided by RICE and contributors "as is" and any
// express or implied warranties, including, but not limited to, the
// implied warranties of merchantability and fitness for a particular
// purpose are disclaimed. In no event shall RICE or contributors be
// liable for any direct, indirect, incidental, special, exemplary, or
// consequential damages (including, but not limited to, procurement of
// substitute goods or services; loss of use, data, or profits; or
// business interruption) however caused and on any theory of liability,
// whether in contract, strict liability, or tort (including negligence
// or otherwise) arising in any way out of the use of this software, even
// if advised of the possibility of such damage. 
// 
// ******************************************************* EndRiceCopyright *

//************************ System Include Files ******************************

#include <iostream>
using std::cerr;
using std::endl;

#include <string>
using std::string;

#ifdef NO_STD_CHEADERS
# include <stdlib.h>
#else
# include <cstdlib> // for 'exit'
using std::exit; // For compatibility with non-std C headers
#endif

//*********************** Xerces Include Files *******************************

#include <xercesc/util/XMLString.hpp> 
using XERCES_CPP_NAMESPACE::XMLString;

#include <xercesc/parsers/XercesDOMParser.hpp>
using XERCES_CPP_NAMESPACE::XercesDOMParser;

#include <xercesc/dom/DOMNamedNodeMap.hpp> 
using XERCES_CPP_NAMESPACE::DOMNamedNodeMap;


//************************* User Include Files *******************************

#include "ConfigParser.hpp"

#include <lib/analysis/DerivedPerfMetrics.hpp>

#include <lib/prof-juicy-x/MathMLExprParser.hpp>

#include <lib/prof-juicy/PgmScopeTree.hpp>
#include <lib/prof-juicy/PerfMetric.hpp>

#include <lib/support/Trace.hpp>


//************************ Forward Declarations ******************************

#define DBG 0

//****************************************************************************

static void ProcessDOCUMENT(DOMNode *node, Args& args, Driver &driver);


//***************************************************************************
// 
//***************************************************************************

ConfigParser::ConfigParser(const string& inputFile, 
			   XercesErrorHandler& errHndlr)
{
  DIAG_DevMsgIf(DBG, "CONFIG: " << inputFile);
  
  mParser = new XercesDOMParser;
  mParser->setValidationScheme(XercesDOMParser::Val_Auto);
  mParser->setErrorHandler(&errHndlr);
  mParser->parse(inputFile.c_str());
  if (mParser->getErrorCount() > 0) {
    ConfigParser_Throw("terminating because of previously reported CONFIGURATION file parse errors.");
  }
  
  mDoc = mParser->getDocument();
  DIAG_DevMsgIf(DBG, "CONFIG: "<< "document: " << mDoc);
}


ConfigParser::~ConfigParser()
{
  delete mParser;
}


void
ConfigParser::parse(Args& args, Driver& driver)
{
  ProcessDOCUMENT(mDoc, args, driver);
}


//***************************************************************************
// 
//***************************************************************************

static void ProcessHPCVIEW(DOMNode *node, Args& args, Driver &driver);
static void ProcessELEMENT(DOMNode *node, Args& args, Driver &driver);
static void ProcessMETRIC(DOMNode *node, Args& args, Driver &driver);
static void ProcessFILE(DOMNode *fileNode, Args& args, Driver &driver, 
			const string& metricNm, bool metricDoDisp, 
			bool metricDoPercent, bool metricDoSortBy, 
			const string& metricDispNm);

static string getAttr(DOMNode *node, const XMLCh *attrName);

static string 
canonicalizePaths(string& inPath, string& outPath);

static void 
printName(DOMNode *node)
{
  const XMLCh *nodeName = node->getNodeName();
  const XMLCh *nodeValue = node->getNodeValue();
  cerr << "name=" << XMLString::transcode(nodeName) 
       << " value=" << XMLString::transcode(nodeValue) << endl;
}


static void 
ProcessDOCUMENT(DOMNode *node, Args& args, Driver &driver)
{
  DOMNode *child = node->getFirstChild();
  ProcessHPCVIEW(child, args, driver);
}


static void 
ProcessHPCVIEW(DOMNode *node, Args& args, Driver &driver)
{
  DIAG_DevMsgIf(DBG, "CONFIG: " << node);

  if ((node == NULL) ||
      (node->getNodeType() != DOMNode::DOCUMENT_TYPE_NODE) ){ 
    ConfigParser_Throw("CONFIGURATION file does not begin with <HPCVIEW>");
  };
  
  node = node->getNextSibling();
  DIAG_DevMsgIf(DBG, "CONFIG: " << node);

  if ( (node == NULL)
       || (node->getNodeType() != DOMNode::ELEMENT_NODE)) {
    ConfigParser_Throw("No DOCUMENT_NODE found in CONFIGURATION file.");
  };

  // process each child 
  DOMNode *child = node->getFirstChild();
  for (; child != NULL; child = child->getNextSibling()) {
    ProcessELEMENT(child, args, driver);
  }
} 


static void 
ProcessELEMENT(DOMNode *node, Args& args, Driver &driver)
{
  static XMLCh* METRIC       = XMLString::transcode("METRIC");
  static XMLCh* PATH         = XMLString::transcode("PATH");
  static XMLCh* REPLACE      = XMLString::transcode("REPLACE");
  static XMLCh* TITLE        = XMLString::transcode("TITLE");
  static XMLCh* STRUCTURE    = XMLString::transcode("STRUCTURE");
  static XMLCh* GROUP        = XMLString::transcode("GROUP");
  static XMLCh* NAMEATTR     = XMLString::transcode("name");
  static XMLCh* VIEWNAMEATTR = XMLString::transcode("viewname");
  static XMLCh* INATTR       = XMLString::transcode("in");
  static XMLCh* OUTATTR      = XMLString::transcode("out");

  const XMLCh *nodeName = node->getNodeName();

  // Verify node type
  if (node->getNodeType() == DOMNode::TEXT_NODE) {
    return; // DTD ensures this can't contain anything but white space
  }
  else if (node->getNodeType() == DOMNode::COMMENT_NODE) {
    return;
  }
  else if (node->getNodeType() != DOMNode::ELEMENT_NODE) {
    ConfigParser_Throw("Unexpected XML object found: '" << XMLString::transcode(nodeName) << "'");
  }

  // Parse ELEMENT nodes
  if (XMLString::equals(nodeName, TITLE)) {
    string title = getAttr(node, NAMEATTR); 
    DIAG_DevMsgIf(DBG, "CONFIG: " << "TITLE: " << title);
    args.title = title;
  }
  else if (XMLString::equals(nodeName, PATH)) {
    string path = getAttr(node, NAMEATTR);
    string viewname = getAttr(node, VIEWNAMEATTR);
    DIAG_DevMsgIf(DBG, "CONFIG: " << "PATH: " << path << ", v=" << viewname);
      
    if (path.empty()) {
      ConfigParser_Throw("PATH name attribute cannot be empty.");
    }
    else if (args.db_copySrcFiles && viewname.empty()) {
      ConfigParser_Throw("PATH '" << path << "': viewname attribute cannot be empty when source files are to be copied.");
    } // there could be many other nefarious values of these attributes
      
    args.searchPathTpls.push_back(Analysis::PathTuple(path, viewname));
  }
  else if (XMLString::equals(nodeName, STRUCTURE)) {
    string fnm = getAttr(node, NAMEATTR); // file name
    DIAG_DevMsgIf(DBG, "CONFIG: " << "STRUCTURE file: " << fnm);
      
    if (fnm.empty()) {
      ConfigParser_Throw("STRUCTURE file name is empty.");
    } 
    else {
      args.structureFiles.push_back(fnm);
    }
  } 
  else if (XMLString::equals(nodeName, GROUP)) {
    string fnm = getAttr(node, NAMEATTR); // file name
    DIAG_DevMsgIf(DBG, "CONFIG: " << "GROUP file: " << fnm);
      
    if (fnm.empty()) {
      ConfigParser_Throw("GROUP file name is empty.");
    } 
    else {
      args.groupFiles.push_back(fnm);
    }
  } 
  else if (XMLString::equals(nodeName, REPLACE)) {  
    string inPath = getAttr(node, INATTR); 
    string outPath = getAttr(node, OUTATTR); 
    DIAG_DevMsgIf(DBG, "CONFIG: " << "REPLACE: " << inPath << " -to- " << outPath);
      
    bool addPath = true;
    if (inPath == outPath) {
      cerr << "WARNING: REPLACE: Identical 'in' and 'out' paths: '"
	   << inPath << "'!  No action will be performed." << endl;
      addPath = false;
    }
    else if (inPath.empty() && !outPath.empty()) {
      cerr << "WARNING: REPLACE: 'in' path is empty; 'out' path '" << outPath
	   << "' will be prepended to every file path!" << endl;
    }
      
    if (addPath) {
      canonicalizePaths(inPath, outPath);
      args.replaceInPath.push_back(inPath);
      args.replaceOutPath.push_back(outPath);
    }
  }
  else if (XMLString::equals(nodeName, METRIC)) {
    ProcessMETRIC(node, args, driver);
  }
  else {
    ConfigParser_Throw("Unexpected ELEMENT type encountered: '" << XMLString::transcode(nodeName) << "'");
  }
}


static void 
ProcessMETRIC(DOMNode *node, Args& args, Driver &driver)
{
  static XMLCh* FILE = XMLString::transcode("FILE");
  static XMLCh* COMPUTE = XMLString::transcode("COMPUTE");
  static XMLCh* NAMEATTR = XMLString::transcode("name");
  static XMLCh* DISPLAYATTR = XMLString::transcode("display");
  static XMLCh* PERCENTATTR = XMLString::transcode("percent");
  static XMLCh* PROPAGATEATTR = XMLString::transcode("propagate");
  static XMLCh* DISPLAYNAMEATTR = XMLString::transcode("displayName");
  static XMLCh* SORTBYATTR = XMLString::transcode("sortBy");

  // get metric attributes
  string metricNm = getAttr(node, NAMEATTR); 
  string metricDispNm = getAttr(node, DISPLAYNAMEATTR); 
  bool metricDoDisp = (getAttr(node, DISPLAYATTR) == "true"); 
  bool metricDoPercent = (getAttr(node,PERCENTATTR) == "true"); 
  bool metricDoSortBy = (getAttr(node,SORTBYATTR) == "true"); 

  if (metricNm.empty()) {
    ConfigParser_Throw("METRIC: Invalid name: '" << metricNm << "'.");
  }
  else if (NameToPerfDataIndex(metricNm) != UNDEF_METRIC_INDEX) {
    ConfigParser_Throw("METRIC: Metric name '" << metricNm << "' was previously defined.");
  }

  if (metricDispNm.empty()) {
    ConfigParser_Throw("METRIC: Invalid displayName: '" << metricDispNm << "'.");
  }
    
  DIAG_DevMsgIf(DBG, "CONFIG: " << "METRIC: name=" << metricNm
		<< " display=" <<  ((metricDoDisp) ? "true" : "false")
		<< " doPercent=" <<  ((metricDoPercent) ? "true" : "false")
		<< " sortBy=" <<  ((metricDoSortBy) ? "true" : "false")
		<< " metricDispNm=" << metricDispNm);
		
  // should have exactly one child
  DOMNode *metricImpl = node->getFirstChild();

  for ( ; metricImpl != NULL; metricImpl = metricImpl->getNextSibling()) {

    if (metricImpl->getNodeType() == DOMNode::TEXT_NODE) {
      // DTD ensures this can't contain anything but white space
      continue;
    }
    else if (metricImpl->getNodeType() == DOMNode::COMMENT_NODE) {
      continue;
    }
    
    const XMLCh *metricType = metricImpl->getNodeName();

    if (XMLString::equals(metricType, FILE)) {

      ProcessFILE(metricImpl, args, driver, metricNm, 
		  metricDoDisp, metricDoPercent, metricDoSortBy, 
		  metricDispNm);
    }
    else if (XMLString::equals(metricType,COMPUTE)) {
      bool propagateComputed
	= (getAttr(metricImpl, PROPAGATEATTR) == "computed"); 
      DOMNode *child = metricImpl->getFirstChild();
      for (; child != NULL; child = child->getNextSibling()) {
	if (child->getNodeType() == DOMNode::TEXT_NODE) {
	  // DTD ensures this can't contain anything but white space
	  continue;
	}
	else if (child->getNodeType() == DOMNode::COMMENT_NODE) {
	  continue;
	}
	
	driver.addMetric(new ComputedPerfMetric(metricNm, metricDispNm, 
						metricDoDisp, metricDoPercent, 
						metricDoSortBy,
						propagateComputed, child)); 
      }
    } 
    else {
      ConfigParser_Throw("Unexpected METRIC type '" << XMLString::transcode(metricType) << "'.");
    }
  }
}


static void 
ProcessFILE(DOMNode* fileNode, Args& args, Driver& driver, 
	    const string& metricNm, bool metricDoDisp, 
	    bool metricDoPercent, bool metricDoSortBy, 
	    const string& metricDispNm)
{
  static XMLCh* TYPEATTR = XMLString::transcode("type");
  static XMLCh* NAMEATTR = XMLString::transcode("name");
  static XMLCh* SELECTATTR = XMLString::transcode("select");

  string metricFile     = getAttr(fileNode, NAMEATTR); 
  string metricFileType = getAttr(fileNode, TYPEATTR);
  string nativeNm     = getAttr(fileNode, SELECTATTR);
  
  if (nativeNm.empty()) {
    //    string error = "FILE: Invalid select attribute '" + nativeNm + "'";
    //    throw DocException(error);
    //  Default is to select "0"
    nativeNm = "0";
  }

  if (!metricFile.empty()) { 
    driver.addMetric(new FilePerfMetric(metricNm, nativeNm, metricDispNm, 
					metricDoDisp, metricDoPercent, 
					metricDoSortBy, metricFile, 
					metricFileType));
  } 
  else {
    ConfigParser_Throw("METRIC '" << metricNm << "' FILE name empty.");
  }
}

// -------------------------------------------------------------------------

// Returns the attribute value or an empty string if the attribute
// does not exist.
static string 
getAttr(DOMNode *node, const XMLCh *attrName)
{
  DOMNamedNodeMap  *attributes = node->getAttributes();
  DOMNode *attrNode = attributes->getNamedItem(attrName);
  DIAG_DevMsgIf(DBG, "CONFIG: " << "getAttr(): attrNode" << attrNode);
  if (attrNode == NULL) {
    return "";
  } 
  else {
    const XMLCh *attrNodeValue = attrNode->getNodeValue();
    return XMLString::transcode(attrNodeValue);
  }
}

// -------------------------------------------------------------------------

static string 
canonicalizePaths(string& inPath, string& outPath)
{
  // Add a '/' at the end of the in path; it's good when testing for
  // equality, to make sure that the last directory in the path is not
  // a prefix of the tested path.  
  // If we need to add a '/' to the in path, then add one to the out
  // path, too because when is time to replace we don't know if we
  // added one or not to the IN path.
  if (!inPath.empty() && inPath[inPath.length()-1] != '/') {
    inPath += '/';
    outPath += '/';
  }
}

