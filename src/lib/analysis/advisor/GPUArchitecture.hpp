// -*-Mode: C++;-*-

// * BeginRiceCopyright *****************************************************
//
// $HeadURL$
// $Id$
//
// --------------------------------------------------------------------------
// Part of HPCToolkit (hpctoolkit.org)
//
// Information about sources of support for research and development of
// HPCToolkit is at 'hpctoolkit.org' and in 'README.Acknowledgments'.
// --------------------------------------------------------------------------
//
// Copyright ((c)) 2002-2018, Rice University
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

//***************************************************************************
//
// File:
//   $HeadURL$
//
// Purpose:
//   [The purpose of this file]
//
// Description:
//   [The set of functions, macros, etc. defined in the file]
//
//***************************************************************************

#ifndef Analysis_Advisor_GPUArchitecture_hpp 
#define Analysis_Advisor_GPUArchitecture_hpp

//************************* System Include Files ****************************

#include <iostream>
#include <vector>
#include <stack>
#include <string>

//*************************** User Include Files ****************************

#include <include/uint.h>

#include <lib/prof/CallPath-Profile.hpp>
#include <lib/prof/Struct-Tree.hpp>


namespace Analysis {

class GPUArchitecture {
 public:
  enum Vendor {
    VENDOR_NVIDIA = 0,
    VENDOR_AMD = 1,
    VENDOR_UNKNOWN = 2
  };

  GPUArchitecture(Vendor vendor) : _vendor(vendor) {}

  // instruction latency
  // memory instruction latency varies
  // <min, max>
  virtual std::pair<int, int> latency(const std::string &opcode) const = 0;

  // warp throughput, not block throughput
  virtual int issue(const std::string &opcode) const = 0;

  virtual int inst_size() const = 0;

  // number of sms per GPU
  virtual int sms() const = 0;

  // number of schedulers per sm
  virtual int schedulers() const = 0;

  // number of warps per sm
  virtual int warps() const = 0;

  // number of threads per warp
  virtual int warp_size() const = 0;

  virtual double frequency() const = 0;

  virtual ~GPUArchitecture() {}

 protected:
  Vendor _vendor;
};


class V100 : public GPUArchitecture {
 public:
  V100() : GPUArchitecture(VENDOR_NVIDIA) {}

  virtual std::pair<int, int> latency(const std::string &opcode) const;

  virtual int issue(const std::string &opcode) const;

  virtual int inst_size() const {
    return 16;
  }

  virtual int sms() const {
    return 80;
  }

  virtual int schedulers() const {
    return 4;
  }

  virtual int warps() const {
    return 64;
  }

  virtual int warp_size() const {
    return 32;
  }

  virtual double frequency() const {
    return 1.38;
  }

  virtual ~V100() {}

 protected:
  Vendor _vendor;
};


}  // Analysis

#endif  // Analysis_Advisor_GPUArchitecture_hpp

