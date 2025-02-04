/* Copyright (c) 2011-2014 PLUMgrid, http://plumgrid.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#ifndef _UAPI__LINUX_BPF_H__
#define _UAPI__LINUX_BPF_H__

#include <linux/types.h>

/* Extended instruction set based on top of classic BPF */

/* instruction classes */
#define BPF_ALU64	0x07	/* alu mode in double word width */

/* ld/ldx fields */
#define BPF_DW		0x18	/* double word */
#define BPF_XADD	0xc0	/* exclusive add */

/* alu/jmp fields */
#define BPF_MOV		0xb0	/* mov reg to reg */
#define BPF_ARSH	0xc0	/* sign extending arithmetic shift right */

/* change endianness of a register */
#define BPF_END		0xd0	/* flags for endianness conversion: */
#define BPF_TO_LE	0x00	/* convert to little-endian */
#define BPF_TO_BE	0x08	/* convert to big-endian */
#define BPF_FROM_LE	BPF_TO_LE
#define BPF_FROM_BE	BPF_TO_BE

#define BPF_JNE		0x50	/* jump != */
#define BPF_JSGT	0x60	/* SGT is signed '>', GT in x86 */
#define BPF_JSGE	0x70	/* SGE is signed '>=', GE in x86 */
#define BPF_CALL	0x80	/* function call */
#define BPF_EXIT	0x90	/* function return */

/* Register numbers */
enum {
	BPF_REG_0 = 0,
	BPF_REG_1,
	BPF_REG_2,
	BPF_REG_3,
	BPF_REG_4,
	BPF_REG_5,
	BPF_REG_6,
	BPF_REG_7,
	BPF_REG_8,
	BPF_REG_9,
	BPF_REG_10,
	__MAX_BPF_REG,
};

/* BPF has 10 general purpose 64-bit registers and stack frame. */
#define MAX_BPF_REG	__MAX_BPF_REG

struct bpf_insn {
	__u8	code;		/* opcode */
	__u8	dst_reg:4;	/* dest register */
	__u8	src_reg:4;	/* source register */
	__s16	off;		/* signed offset */
	__s32	imm;		/* signed immediate constant */
};

/* BPF syscall commands */
enum bpf_cmd {
	/* create a map with given type and attributes
	 * fd = bpf(BPF_MAP_CREATE, union bpf_attr *, u32 size)
	 * returns fd or negative error
	 * map is deleted when fd is closed
	 */
	BPF_MAP_CREATE,

	/* verify and load eBPF program
	 * prog_fd = bpf(BPF_PROG_LOAD, union bpf_attr *attr, u32 size)
	 * Using attr->prog_type, attr->insns, attr->license
	 * returns fd or negative error
	 */
	BPF_PROG_LOAD,

	/* lookup key in a given map
	 * err = bpf(BPF_MAP_LOOKUP_ELEM, union bpf_attr *attr, u32 size)
	 * Using attr->map_fd, attr->key, attr->value
	 * returns zero and stores found elem into value
	 * or negative error
	 */
	BPF_MAP_LOOKUP_ELEM,

	/* create or update key/value pair in a given map
	 * err = bpf(BPF_MAP_UPDATE_ELEM, union bpf_attr *attr, u32 size)
	 * Using attr->map_fd, attr->key, attr->value
	 * returns zero or negative error
	 */
	BPF_MAP_UPDATE_ELEM,

	/* find and delete elem by key in a given map
	 * err = bpf(BPF_MAP_DELETE_ELEM, union bpf_attr *attr, u32 size)
	 * Using attr->map_fd, attr->key
	 * returns zero or negative error
	 */
	BPF_MAP_DELETE_ELEM,

	/* lookup key in a given map and return next key
	 * err = bpf(BPF_MAP_GET_NEXT_KEY, union bpf_attr *attr, u32 size)
	 * Using attr->map_fd, attr->key, attr->next_key
	 * returns zero and stores next key or negative error
	 */
	BPF_MAP_GET_NEXT_KEY,
};

enum bpf_map_type {
	BPF_MAP_TYPE_UNSPEC,
	BPF_PROG_TYPE_XDP,
};

enum bpf_prog_type {
	BPF_PROG_TYPE_UNSPEC,
};

union bpf_attr {
	struct { /* anonymous struct used by BPF_MAP_CREATE command */
		__u32	map_type;	/* one of enum bpf_map_type */
		__u32	key_size;	/* size of key in bytes */
		__u32	value_size;	/* size of value in bytes */
		__u32	max_entries;	/* max number of entries in a map */
	};

	struct { /* anonymous struct used by BPF_MAP_*_ELEM commands */
		__u32		map_fd;
		__aligned_u64	key;
		union {
			__aligned_u64 value;
			__aligned_u64 next_key;
		};
	};
} __attribute__((aligned(8)));

/* User return codes for XDP prog type.
 * A valid XDP program must return one of these defined values. All other
 * return codes are reserved for future use. Unknown return codes will result
 * in packet drop.
 */
enum xdp_action {
	XDP_ABORTED = 0,
	XDP_DROP,
	XDP_PASS,
	XDP_TX,
};

/* user accessible metadata for XDP packet hook
 * new fields must be added to the end of this structure
 */
struct xdp_md {
	__u32 data;
	__u32 data_end;
};

#define XDP_PACKET_HEADROOM 256

/* integer value in 'imm' field of BPF_CALL instruction selects which helper
 * function eBPF program intends to call
 */
enum bpf_func_id {
	BPF_FUNC_unspec,
	__BPF_FUNC_MAX_ID,
};

#endif /* _UAPI__LINUX_BPF_H__ */
