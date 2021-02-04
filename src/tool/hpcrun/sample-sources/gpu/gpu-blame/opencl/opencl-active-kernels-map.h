#ifndef gpu_blame_opencl_active_kernels_map_h_
#define gpu_blame_opencl_active_kernels_map_h_

//******************************************************************************
// interface operations
//******************************************************************************

typedef struct active_kernels_entry_t active_kernels_entry_t;

active_kernels_entry_t*
active_kernels_lookup
(
 uint64_t ak_id
);


void
active_kernels_insert
(
 uint64_t ak_id,
 event_list_node_t *event_node
);


void
active_kernels_delete
(
 uint64_t ak_id
);


void
active_kernels_forall
(
 splay_visit_t visit_type,
 void (*fn),
 void *arg
);


long
active_kernels_size
(
	void
);


void
increment_blame_for_entry
(
 active_kernels_entry_t *entry,
 double blame
);

#endif		// gpu_blame_opencl_active_kernels_map_h_