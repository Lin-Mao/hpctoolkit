#ifndef blame_shift_helper_opencl_h
#define blame_shift_helper_opencl_h

//******************************************************************************
// local includes
//******************************************************************************

#include "opencl-event-map.h"		// event_list_node_t, queue_node_t



//******************************************************************************
// interface operations
//******************************************************************************

void
calculate_blame_for_active_kernels
(
 event_list_node_t *event_list,
 struct timespec sync_start,
 struct timespec sync_end
);

#endif 	//blame_shift_helper_opencl_h