/*
 * The contents of this file are subject to the MonetDB Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.monetdb.org/Legal/MonetDBLicense
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the
 * License for the specific language governing rights and limitations
 * under the License.
 *
 * The Original Code is the MonetDB Database System.
 *
 * The Initial Developer of the Original Code is CWI.
 * Portions created by CWI are Copyright (C) 1997-July 2008 CWI.
 * Copyright August 2008-2013 MonetDB B.V.
 * All Rights Reserved.
 */

/*
 * @f counters
 * @a S. Manegold, P. Boncz
 * @+ Performance Counters
 *
 * This is a memory/cpu performance measurement tool for
 * the following processor (families).
 * @itemize
 * @item MIPS R10000/R12000 (IP27)
 * @item Sun UltraSparcI/II (sun4u)
 * @item Intel Pentium (i586/P5)
 * @item Intel PentiumPro/PentiumII/PentiumIII/Celeron (i686/P6)
 * @item AMD Athlon (i686/K7)
 * @item Intel Itanium/Itanium2 (ia64)
 * @end itemize
 *
 * It uses
 * @itemize
 * @item @url{http://www.cse.msu.edu/~enbody/perfmon.html,libperfmon } libperfex (IRIX) for R10000/R12000,
 * @item (Solaris <= 7) by Richard Enbody, libcpc (Solaris >= 8) for UltraSparcI/II,
 * @item @url{http://user.it.uu.se/~mikpe/linux/perfctr/,libperfctr} (Linux-i?86 >= 2.4), by M. Pettersson for Pentiums & Athlons.
 * @item @url{http://www.hpl.hp.com/research/linux/perfmon/, libpfm} (Linux-ia64 >= 2.4), by HP for Itanium[2].
 * @end itemize
 *
 * Module counters provides similar interface and facilities as Peter's
 * R10000 perfex module, but it offers no multiplexing of several events;
 * only two events can be monitored at a time.
 * On non-Linux/x86, non-Solaris/UltraSparc, and non-IRIX/R1x000 systems,
 * only the elapsed time in microseconds is measured.
 *
 * @+ Module Definition
 * The major difference with the M4 library is that it does not
 * expose the counter structure to the language level.
 * This is possible, because the M4 scheme for their decomposition
 * right now is limited to turn it into a BAT or string.
 *
 * Instead an integer handle is return to designate a counter.
 * We provide some BAT views over the counter table
 */
#include "monetdb_config.h"
#include "counters.h"
#include "errno.h"

#ifdef WIN32
#if !defined(LIBMAL) && !defined(LIBATOMS) && !defined(LIBKERNEL) && !defined(LIBMAL) && !defined(LIBOPTIMIZER) && !defined(LIBSCHEDULER) && !defined(LIBMONETDB5)
#define counters_export extern __declspec(dllimport)
#else
#define counters_export extern __declspec(dllexport)
#endif
#else
#define counters_export extern
#endif

int NumEvents = 0, NoEvent = 0;

#if defined(HWCOUNTERS)

#if defined(HW_SunOS)
#if   defined(HAVE_LIBPERFMON)	/* libperfmon (on Solaris <= 7) */
#define LIB_SunOS(a,b) a
#elif defined(HAVE_LIBCPC)	/* libcpc     (on Solaris >= 8) */
#define LIB_SunOS(a,b) b
#endif
#endif

typedef struct {
	int id0;		/* event id for counter 0 */
	int id1;		/* event id for counter 1 */
	str unified;		/* unified event name */
	str native;		/* native event name */
#if ( defined(HW_Linux) && ( defined(HW_i568) || defined(HW_i686) || defined(HW_x86_64) ) )
	int code;		/* event code */
	int mask;		/* unit mask for "fine-tuning" of some events */
#endif
#if defined(HW_SunOS)
	 LIB_SunOS(int bits;	/* event code for perfmon on Solaris <= 7 */
		   , char *spec;	/* event spec for libcpc  on Solaris >= 8 */
	    )
#endif
} event_t;

event_t NO_event[1] = { {00, 00, str_nil, str_nil
#if ( defined(HW_Linux) && ( defined(HW_i568) || defined(HW_i686) || defined(HW_x86_64) ) )
			 , 00, 00
#endif
#if defined(HW_SunOS)
			 , LIB_SunOS(0, NULL)
#endif
			 }
};
event_t *event = NO_event;

#if ( defined(HW_Linux) && ( defined(HW_i568) || defined(HW_i686) || defined(HW_x86_64) ) )

#if defined(HAVE_LIBPPERF)
#include <sys/utsname.h>
#include <libpperf.h>
   /* count in user mode, only */
   /* libpperf takes care of swapping bits for P6 & K7 */
#define CPL 2
#elif defined(HAVE_LIBPERFCTR)
#include <libperfctr.h>
struct perfctr_info Info;
#endif

#define X_NUMEVENTS 0
event_t *X_event = NO_event;

  /* P5 (i586) Pentium */

#if defined(HAVE_LIBPERFCTR)
	/* sub-fields in the Control and Event Select Register (CESR)
	 *  CC0, CC1: CPL Level to Monitor, possibilities are
	 *  000 = Count Nothing
	 *  001 = Count Event while CPL = 0,1,2
	 *  010 = Count Event while CPL = 3
	 *  011 = Count Event while CPL = 0,1,2,3
	 *  100 = Count Nothing
	 *  101 = Count Clocks while CPL = 0,1,2
	 *  110 = Count Clocks while CPL = 3
	 *  111 = Count Clocks while CPL = 0,1,2,3
	 *  Here we only use 000, 001, 010 and 011.
	 */
typedef union {
	unsigned int word;	/* to initialize in one assignment */
	struct p5_cesr {
		unsigned int es0:6;	/* event select counter 0 */
		unsigned int cc0:3;	/* counter control 0 (see above) */
		unsigned int pc0:1;	/* pin control bit 0
					   0=event increment, 1=event overflow */
		unsigned int re0:6;	/* reserved */
		unsigned int es1:6;	/* event select counter 1 */
		unsigned int cc1:3;	/* counter control 1 (see cc0) */
		unsigned int pc1:1;	/* pin control bit 1 */
		unsigned int re1:6;	/* reserved */
	} cesr;
} P5_cesr_t;

	/* defaults:
	 * P5_cesr.cesr.cc0 = P5_cesr.cesr.cc1 = 2; (count in user mode, only)
	 */
P5_cesr_t P5_cesr = { (2 << 6) | (2 << 22) };
#endif

#define P5_NUMEVENTS 40
event_t P5_event[P5_NUMEVENTS + 1] = {
	{0, 0, str_nil, "data_read_hits", 0x00, 0},	/* P5_MEM_DATA_READ         , "mem_data_read"         ,  0 ,  0 , P5_DATA_READ                                   , */
	{1, 1, str_nil, "data_write_hits", 0x01, 0},	/* P5_MEM_DATA_WRITE        , "mem_data_write"        ,  1 ,  1 , P5_DATA_WRITE                                  , */
	{2, 2, "TLB_misses", "data_TLB_misses", 0x02, 0},	/* P5_TLB_MISS              , "tlb_miss"              ,  2 ,  2 , P5_DATA_TLB_MISS                               , */
	{3, 3, str_nil, "data_read_misses", 0x03, 0},	/* P5_MEM_DATA_RM           , "mem_data_rm"           ,  3 ,  3 , P5_DATA_READ_MISS                              , */
	{4, 4, str_nil, "data_write_misses", 0x04, 0},	/* P5_MEM_DATA_WM           , "mem_data_wm"           ,  4 ,  4 , P5_DATA_WRITE_MISS                             , */
	{5, 5, str_nil, "writes_(hits)_to_M/E", 0x05, 0},	/* P5_WRITE_HIT_ME          , "write_hit_me"          ,  5 ,  5 , P5_WRITE_HIT_TO_M_OR_E_STATE_LINES             , */
	{6, 6, str_nil, "data_cache_lines_written_back", 0x06, 0},	/* P5_DATA_CACHE_WB         , "data_cache_wb"         ,  6 ,  6 , P5_DATA_CACHE_LINES_WRITTEN_BACK               , */
	{7, 7, str_nil, "external_snoops", 0x07, 0},	/* P5_EXT_SNOOPS            , "ext_snoops"            ,  7 ,  7 , P5_EXTERNAL_SNOOPS                             , */
	{8, 8, str_nil, "data_cache_snoop_hits", 0x08, 0},	/* P5_DATA_CACHE_SNOOP_HITS , "data_cache_snoop_hits" ,  8 ,  8 , P5_EXTERNAL_DATA_CACHE_SNOOP_HITS              , */
	{9, 9, str_nil, "memory_accesses_in_both_pipes", 0x09, 0},	/* P5_MEM_ACCS_BOTH_PIPES   , "mem_accs_both_pipes"   ,  9 ,  9 , P5_MEMORY_ACCESSES_IN_BOTH_PIPES               , */
	{10, 10, str_nil, "bank_conflicts", 0x0A, 0},	/* P5_BANK_CONFLICTS        , "bank_conflicts"        , 10 , 10 , P5_BANK_CONFLICTS                              , */
	{11, 11, str_nil, "misaligned_data_memory_references", 0x0B, 0},	/* P5_MISAL_MEM_REF         , "misal_mem_ref"         , 11 , 11 , P5_MISALIGNED_DATA_MEMORY_OR_IO_REFERENCES     , */
	{12, 12, str_nil, "code_reads", 0x0C, 0},	/* P5_CODE_READ             , "code_read"             , 12 , 12 , P5_CODE_READ                                   , */
	{13, 13, "iTLB_misses", "code_TLB_misses", 0x0D, 0},	/* P5_CODE_TLB_MISS         , "code_tlb_miss"         , 13 , 13 , P5_CODE_TLB_MISS                               , */
	{14, 14, "L1_inst_misses", "code_cache_misses", 0x0E, 0},	/* P5_CODE_CACHE_MISS       , "code_cache_miss"       , 14 , 14 , P5_CODE_CACHE_MISS                             , */
	{15, 15, str_nil, "segment_register_loaded", 0x0F, 0},	/* P5_SEG_REG_LOAD          , "seg_reg_load"          , 15 , 15 , P5_ANY_SEGMENT_REGISTER_LOADED                 , */
	{16, 16, str_nil, "segment_descriptor_cache_accesses", 0x10, 0},	/* P5_SEG_DESC_CACHE_ACCS   , "seg_desc_cache_accs"   , 16 , -1 , 0                                              , */
	{17, 17, str_nil, "segment_descriptor_cache_hits", 0x11, 0},	/* P5_SEG_DESC_CACHE_HIT    , "seg_desc_cache_hit"    , 17 , -1 , 0                                              , */
	{18, 18, "branches", "branches", 0x12, 0},	/* P5_BRANCHES              , "branches"              , 18 , 16 , P5_BRANCHES                                    , */
	{19, 19, str_nil, "BTB_hits", 0x13, 0},	/* P5_BTB_HITS              , "btb_hits"              , 19 , 17 , P5_BTB_HITS                                    , */
	{20, 20, str_nil, "taken_branches_or_BTB_hits", 0x14, 0},	/* P5_BRANCH_OR_BTB_HIT     , "branch_or_btb_hit"     , 20 , 18 , P5_TAKEN_BRANCH_OR_BTB_HIT                     , */
	{21, 21, str_nil, "pipeline_flushes", 0x15, 0},	/* P5_PIPELINE_FLUSH        , "pipeline_flush"        , 21 , 19 , P5_PIPELINE_FLUSHES                            , */
	{22, 22, str_nil, "instructions_executed_in_both_pipes", 0x16, 0},	/* P5_INS_EXE_B_PIPES       , "ins_exe_b_pipes"       , 22 , 20 , P5_INSTRUCTIONS_EXECUTED                       , */
	{23, 23, str_nil, "instructions_executed_in_V-pipe", 0x17, 0},	/* P5_INS_EXE_V_PIPE        , "ins_exe_v_pipe"        , 23 , 21 , P5_INSTRUCTIONS_EXECUTED_IN_V_PIPE             , */
	{24, 24, str_nil, "clocks_while_bus_cycle_in_progress", 0x18, 0},	/* P5_CLKS_BUS_CYCLE        , "clks_bus_cycle"        , 24 , 22 , P5_BUS_CYCLE_DURATION                          , */
	{25, 25, str_nil, "pipe_stalled_by_full_write_buffers", 0x19, 0},	/* P5_PIPE_STL_FWB          , "pipe_stl_fwb"          , 25 , 23 , P5_WRITE_BUFFER_FULL_STALL_DURATION            , */
	{26, 26, str_nil, "pipe_stalled_by_waiting_for_data_reads", 0x1A, 0},	/* P5_PIPE_STL_WDR          , "pipe_stl_wdr"          , 26 , 24 , P5_WAITING_FOR_DATA_MEMORY_READ_STALL_DURATION , */
	{27, 27, str_nil, "pipe_stalled_by_writes_to_M/E", 0x1B, 0},	/* P5_PIPE_STL_WME          , "pipe_stl_wme"          , 27 , 25 , P5_STALL_ON_WRITE_TO_AN_E_OR_M_STATE_LINE      , */
	{28, 28, str_nil, "locked_bus_cycles", 0x1C, 0},	/* P5_LOCKED_BUS            , "locked_bus"            , 28 , 26 , P5_LOCKED_BUS_CYCLE                            , */
	{29, 29, str_nil, "I/O_read_or_write_cycles", 0x1D, 0},	/* P5_IO_READ_WRITE         , "io_read_write"         , 29 , 27 , P5_IO_READ_OR_WRITE_CYCLE                      , */
	{30, 30, str_nil, "non-cacheable_memory_references", 0x1E, 0},	/* P5_NON_CACHE_MEM_REF     , "non-cache_mem_ref"     , 30 , 28 , P5_NONCACHEABLE_MEMORY_READS                   , */
	{31, 31, str_nil, "pipeline_stalled_by_AGI", 0x1F, 0},	/* P5_PIPE_STL_AGI          , "pipe_stl_agi"          , 31 , 29 , P5_PIPELINE_AGI_STALLS                         , */
	{32, 32, str_nil, "floating-point_operations", 0x22, 0},	/* P5_FLOPS                 , "flops"                 , 32 , 30 , P5_FLOPS                                       , */
	{33, 33, str_nil, "breakpoint_matches_on_DR0", 0x23, 0},	/* P5_BRK_DR0               , "brk_dr0"               , 33 , 31 , P5_BREAKPOINT_MATCH_ON_DR0_REGISTER            , */
	{34, 34, str_nil, "breakpoint_matches_on_DR1", 0x24, 0},	/* P5_BRK_DR1               , "brk_dr1"               , 34 , 32 , P5_BREAKPOINT_MATCH_ON_DR1_REGISTER            , */
	{35, 35, str_nil, "breakpoint_matches_on_DR2", 0x25, 0},	/* P5_BRK_DR2               , "brk_dr2"               , 35 , 33 , P5_BREAKPOINT_MATCH_ON_DR2_REGISTER            , */
	{36, 36, str_nil, "breakpoint_matches_on_DR3", 0x26, 0},	/* P5_BRK_DR3               , "brk_dr3"               , 36 , 34 , P5_BREAKPOINT_MATCH_ON_DR3_REGISTER            , */
	{37, 37, str_nil, "hardware_interrupts", 0x27, 0},	/* P5_HDW_INT               , "hdw_int"               , 37 , 35 , P5_HARDWARE_INTERRUPTS                         , */
	{38, 38, str_nil, "data_reads_or_writes", 0x28, 0},	/* P5_MEM_READ_WRITE_HIT    , "mem_read_write_hit"    , 38 , 36 , P5_DATA_READ_OR_WRITE                          , */
	{39, 39, "L1_data_misses", "data_read/write_misses", 0x29, 0},	/* P5_MEM_READ_WRITE_MISS   , "mem_read_write_miss"   , 39 , 37 , P5_DATA_READ_MISS_OR_WRITE_MISS                , */
	{22, 22, str_nil, str_nil, 0x16, 0}
};

  /* P6 (i686) PentiumPro/PentiumII/PentiumIII/Celeron */

#if defined(HAVE_LIBPERFCTR)
typedef union {
	unsigned int word;	/* to initialize in one assignment */
	struct p6_k7_cesr {
		unsigned int evsel:8;	/* event select */
		unsigned int umask:8;	/* further qualifies event (MESI) */
		unsigned int usr:1;	/* count in user mode (CPL=1,2,3) */
		unsigned int os:1;	/* count in os mode (CPL=0) */
		unsigned int e:1;	/* edge detect */
		unsigned int pc:1;	/* pin control */
		unsigned int aint:1;	/* local APIC interrupt enable on overflow */
		unsigned int res:1;	/* reserved */
		unsigned int en:1;	/* enable counters (P6: sel0 only!) */
		unsigned int inv:1;	/* invert counter mask */
		unsigned int cmask:8;	/* if!0, compare with events */
	} cesr;
} P6_K7_cesr_t;

	/* defaults:
	 * P6_K7_cesr0.cesr.usr = P6_K7_cesr1.cesr.usr = 1; (count in user mode, only)
	 * P6_K7_cesr0.cesr.en  = P6_K7_cesr1.cesr.en  = 1;
	 */
P6_K7_cesr_t P6_K7_cesr0 = { ((1 << 16) | (1 << 22)) }, P6_K7_cesr1 = {
((1 << 16) | (1 << 22))};
#endif

#define P6_NUMEVENTS 68
event_t P6_event[P6_NUMEVENTS + 1] = {
	{0, 0, str_nil, "all_memory_references,_cachable_and_non", 0x43, 0},	/* P6_DATA_MEM_REFS                 , "data_mem_refs"                 , P6_DATA_MEM_REFS                 , */
	{1, 1, "L1_data_misses", "total_lines_allocated_in_the_DCU", 0x45, 0},	/* P6_DCU_LINES_IN                  , "dcu_lines_in"                  , P6_DCU_LINES_IN                  , */
	{2, 2, str_nil, "number_of_M_state_lines_allocated_in_DCU", 0x46, 0},	/* P6_DCU_M_LINES_IN                , "dcu_m_lines_in"                , P6_DCU_M_LINES_IN                , */
	{3, 3, str_nil, "number_of_M_lines_evicted_from_the_DCU", 0x47, 0},	/* P6_DCU_M_LINES_OUT               , "dcu_m_lines_out"               , P6_DCU_M_LINES_OUT               , */
	{4, 4, str_nil, "number_of_cycles_while_DCU_miss_outstanding", 0x48, 0},	/* P6_DCU_MISS_OUTSTANDING          , "dcu_miss_outstanding"          , P6_DCU_MISS_OUTSTANDING          , */
	{5, 5, str_nil, "number_of_non/cachable_instruction_fetches", 0x80, 0},	/* P6_IFU_IFETCH                    , "ifu_ifetch"                    , P6_IFU_FETCH                     , */
	{6, 6, "L1_inst_misses", "number_of_instruction_fetch_misses", 0x81, 0},	/* P6_IFU_IFETCH_MISS               , "ifu_ifetch_miss"               , P6_IFU_FETCH_MISS                , */
	{7, 7, "iTLB_misses", "number_of_ITLB_misses", 0x85, 0},	/* P6_ITLB_MISS                     , "itlb_miss"                     , P6_ITLB_MISS                     , */
	{8, 8, str_nil, "cycles_instruction_fetch_pipe_is_stalled", 0x86, 0},	/* P6_IFU_MEM_STALL                 , "ifu_mem_stall"                 , P6_IFU_MEM_STALL                 , */
	{9, 9, str_nil, "cycles_instruction_length_decoder_is_stalled", 0x87, 0},	/* P6_ILD_STALL                     , "ild_stall"                     , P6_ILD_STALL                     , */
	{10, 10, str_nil, "number_of_L2_instruction_fetches", 0x28, 0xF},	/* P6_L2_IFETCH                     , "l2_ifetch"                     , P6_L2_IFETCH                     , */
	{11, 11, str_nil, "number_of_L2_data_loads", 0x29, 0xF},	/* P6_L2_LD                         , "l2_ld"                         , P6_L2_LD                         , */
	{12, 12, str_nil, "number_of_L2_data_stores", 0x2a, 0xF},	/* P6_L2_ST                         , "l2_st"                         , P6_L2_ST                         , */
	{13, 13, "L2_data_misses", "number_of_allocated_lines_in_L2", 0x24, 0},	/* P6_L2_LINES_IN                   , "l2_lines_in"                   , P6_L2_LINES_IN                   , */
	{14, 14, str_nil, "number_of_recovered_lines_from_L2", 0x26, 0},	/* P6_L2_LINES_OUT                  , "l2_lines_out"                  , P6_L2_LINES_OUT                  , */
	{15, 15, str_nil, "number_of_modified_lines_allocated_in_L2", 0x25, 0},	/* P6_L2_M_LINES_INM                , "l2_m_lines_inm"                , P6_L2_M_LINES_INM                , */
	{16, 16, str_nil, "number_of_modified_lines_removed_from_L2", 0x27, 0},	/* P6_L2_M_LINES_OUTM               , "l2_m_lines_outm"               , P6_L2_M_LINES_OUTM               , */
	{17, 17, str_nil, "number_of_L2_requests", 0x2e, 0xF},	/* P6_L2_RQSTS                      , "l2_rqsts"                      , P6_L2_RQSTS                      , */
	{18, 18, str_nil, "number_of_L2_address_strobes", 0x21, 0},	/* P6_L2_ADS                        , "l2_ads"                        , P6_L2_ADS                        , */
	{19, 19, str_nil, "number_of_cycles_data_bus_was_busy", 0x22, 0},	/* P6_L2_DBUS_BUSY                  , "l2_dbus_busy"                  , P6_L2_DBUS_BUSY                  , */
	{20, 20, str_nil, "cycles_data_bus_was_busy_in_xfer_from_L2_to_CPU", 0x23, 0},	/* P6_L2_DMUS_BUSY_RD               , "l2_dmus_busy_rd"               , P6_L2_DBUS_BUSY_RD               , */
	{21, 21, str_nil, "number_of_clocks_DRDY_is_asserted", 0x62, 0},	/* P6_BUS_DRDY_CLOCKS               , "bus_drdy_clocks"               , P6_BUS_DRDY_CLOCKS               , */
	{22, 22, str_nil, "number_of_clocks_LOCK_is_asserted", 0x63, 0},	/* P6_BUS_LOCK_CLOCKS               , "bus_lock_clocks"               , P6_BUS_LOCK_CLOCKS               , */
	{23, 23, str_nil, "number_of_outstanding_bus_requests", 0x60, 0},	/* P6_BUS_REQ_OUTSTANDING           , "bus_req_outstanding"           , P6_BUS_REQ_OUTSTANDING           , */
	{24, 24, str_nil, "number_of_burst_read_transactions", 0x65, 0},	/* P6_BUS_TRAN_BRD                  , "bus_tran_brd"                  , P6_BUS_TRAN_BRD                  , */
	{25, 25, str_nil, "number_of_read_for_ownership_transactions", 0x66, 0},	/* P6_BUS_TRAN_RFO                  , "bus_tran_rfo"                  , P6_BUS_TRAN_RFO                  , */
	{26, 26, str_nil, "number_of_write_back_transactions", 0x67, 0},	/* P6_BUS_TRANS_WB                  , "bus_trans_wb"                  , P6_BUS_TRANS_WB                  , */
	{27, 27, str_nil, "number_of_instruction_fetch_transactions", 0x68, 0},	/* P6_BUS_TRAN_IFETCH               , "bus_tran_ifetch"               , P6_BUS_TRAN_IFETCH               , */
	{28, 28, str_nil, "number_of_invalidate_transactions", 0x69, 0},	/* P6_BUS_TRAN_INVAL                , "bus_tran_inval"                , P6_BUS_TRAN_INVAL                , */
	{29, 29, str_nil, "number_of_partial_write_transactions", 0x6a, 0},	/* P6_BUS_TRAN_PWR                  , "bus_tran_pwr"                  , P6_BUS_TRAN_PWR                  , */
	{30, 30, str_nil, "number_of_partial_transactions", 0x6b, 0},	/* P6_BUS_TRANS_P                   , "bus_trans_p"                   , P6_BUS_TRANS_P                   , */
	{31, 31, str_nil, "number_of_I/O_transactions", 0x6c, 0},	/* P6_BUS_TRANS_IO                  , "bus_trans_io"                  , P6_BUS_TRANS_IO                  , */
	{32, 32, str_nil, "number_of_deferred_transactions", 0x6d, 0},	/* P6_BUS_TRANS_DEF                 , "bus_trans_def"                 , P6_BUS_TRAN_DEF                  , */
	{33, 33, str_nil, "number_of_burst_transactions", 0x6e, 0},	/* P6_BUS_TRAN_BURST                , "bus_tran_burst"                , P6_BUS_TRAN_BURST                , */
	{34, 34, str_nil, "number_of_all_transactions", 0x70, 0},	/* P6_BUS_TRAN_ANY                  , "bus_tran_any"                  , P6_BUS_TRAN_ANY                  , */
	{35, 35, str_nil, "number_of_memory_transactions", 0x6f, 0},	/* P6_BUS_TRAN_MEM                  , "bus_tran_mem"                  , P6_BUS_TRAN_MEM                  , */
	{36, 36, str_nil, "bus_cycles_this_processor_is_receiving_data", 0x64, 0},	/* P6_BUS_DATA_RCV                  , "bus_data_rcv"                  , P6_BUS_DATA_RCV                  , */
	{37, 37, str_nil, "bus_cycles_this_processor_is_driving_BNR_pin", 0x61, 0},	/* P6_BUS_BNR_DRV                   , "bus_bnr_drv"                   , P6_BUS_BNR_DRV                   , */
	{38, 38, str_nil, "bus_cycles_this_processor_is_driving_HIT_pin", 0x7a, 0},	/* P6_BUS_HIT_DRV                   , "bus_hit_drv"                   , P6_BUS_HIT_DRV                   , */
	{39, 39, str_nil, "bus_cycles_this_processor_is_driving_HITM_pin", 0x7b, 0},	/* P6_BUS_HITM_DRV                  , "bus_hitm_drv"                  , P6_BUS_HITM_DRV                  , */
	{40, 40, str_nil, "cycles_during_bus_snoop_stall", 0x7e, 0},	/* P6_BUS_SNOOP_STALL               , "bus_snoop_stall"               , P6_BUS_SNOOP_STALL               , */
	{41, -1, str_nil, "number_of_computational_FP_operations_retired", 0xc1, 0},	/* P6_COMP_FLOP_RET                 , "comp_flop_ret"                 , P6_FLOPS                         , */
	{42, -1, str_nil, "number_of_computational_FP_operations_executed", 0x10, 0},	/* P6_FLOPS                         , "flops"                         , P6_FP_COMP_OPS_EXE               , */
	{-1, 43, str_nil, "number_of_FP_execptions_handled_by_microcode", 0x11, 0},	/* P6_FP_ASSIST                     , "fp_assist"                     , P6_FP_ASSIST                     , */
	{-1, 44, str_nil, "number_of_multiplies", 0x12, 0},	/* P6_MUL                           , "mul"                           , P6_MUL                           , */
	{-1, 45, str_nil, "number_of_divides", 0x13, 0},	/* P6_DIV                           , "div"                           , P6_DIV                           , */
	{46, -1, str_nil, "cycles_divider_is_busy", 0x14, 0},	/* P6_CYCLES_DIV_BUSY               , "cycles_div_busy"               , P6_CYCLES_DIV_BUSY               , */
	{47, 47, str_nil, "number_of_store_buffer_blocks", 0x03, 0},	/* P6_LD_BLOCKS                     , "ld_blocks"                     , P6_LD_BLOCKS                     , */
	{48, 48, str_nil, "number_of_store_buffer_drain_cycles", 0x04, 0},	/* P6_SB_DRAINS                     , "sb_drains"                     , P6_SB_DRAINS                     , */
	{49, 49, str_nil, "number_of_misaligned_data_memory_references", 0x05, 0},	/* P6_MISALIGN_MEM_REF              , "misalign_mem_ref"              , P6_MISALIGN_MEM_REF              , */
	{50, 50, str_nil, "number_of_instructions_retired", 0xc0, 0},	/* P6_INST_RETIRED                  , "inst_retired"                  , P6_INST_RETIRED                  , */
	{51, 51, str_nil, "number_of_UOPs_retired", 0xc2, 0},	/* P6_UOPS_RETIRED                  , "uops_retired"                  , P6_UOPS_RETIRED                  , */
	{52, 52, str_nil, "number_of_instructions_decoded", 0xd0, 0},	/* P6_INST_DECODER                  , "inst_decoder"                  , P6_INST_DECODED                  , */
	{53, 53, str_nil, "number_of_hardware_interrupts_received", 0xc8, 0},	/* P6_HW_INT_RX                     , "hw_int_rx"                     , P6_HW_INT_RX                     , */
	{54, 54, str_nil, "cycles_interrupts_are_disabled", 0xc6, 0},	/* P6_CYCLES_INT_MASKED             , "cycles_int_masked"             , P6_CYCLES_INT_MASKED             , */
	{55, 55, str_nil, "cycles_interrupts_are_disabled_with_pending_interrupts", 0xc7, 0},	/* P6_CYCLES_INT_PENDING_AND_MASKED , "cycles_int_pending_and_masked" , P6_CYCLES_INT_PENDING_AND_MASKED , */
	{56, 56, "branches", "number_of_branch_instructions_retired", 0xc4, 0},	/* P6_BR_INST_RETIRED               , "br_inst_retired"               , P6_BR_INST_RETIRED               , */
	{57, 57, "branch_misses", "number_of_mispredicted_branches_retired", 0xc5, 0},	/* P6_BR_MISS_PRED_RETIRED          , "br_miss_pred_retired"          , P6_BR_MISS_PRED_RETIRED          , */
	{58, 58, "Tbranches", "number_of_taken_branches_retired", 0xc9, 0},	/* P6_BR_TAKEN_RETIRED              , "br_taken_retired"              , P6_BR_TAKEN_RETIRED              , */
	{59, 59, "Tbranch_misses", "number_of_taken_mispredictions_branches_retired", 0xca, 0},	/* P6_BR_MISS_PRED_TAKEN_RET        , "br_miss_pred_taken_ret"        , P6_BR_MISS_PRED_TAKEN_RET        , */
	{60, 60, str_nil, "number_of_branch_instructions_decoded", 0xe0, 0},	/* P6_BR_INST_DECODED               , "br_inst_decoded"               , P6_BR_INST_DECODED               , */
	{61, 61, str_nil, "number_of_branches_that_miss_the_BTB", 0xe2, 0},	/* P6_BTB_MISSES                    , "btb_misses"                    , P6_BTB_MISSES                    , */
	{62, 62, str_nil, "number_of_bogus_branches", 0xe4, 0},	/* P6_BR_BOGUS                      , "br_bogus"                      , P6_BR_BOGUS                      , */
	{63, 63, str_nil, "number_of_times_BACLEAR_is_asserted", 0xe6, 0},	/* P6_BACLEARS                      , "baclears"                      , P6_BACLEARS                      , */
	{64, 64, str_nil, "cycles_during_resource_related_stalls", 0xa2, 0},	/* P6_RESOURCE_STALLS               , "resource_stalls"               , P6_RESOURCE_STALLS               , */
	{65, 65, str_nil, "cycles_or_events_for_partial_stalls", 0xd2, 0},	/* P6_PARTIAL_RAT_STALLS            , "partial_rat_stalls"            , P6_PARTIAL_RAT_STALLS            , */
	{66, 66, str_nil, "number_of_segment_register_loads", 0x06, 0},	/* P6_SEGMENT_REG_LOADS             , "segment_reg_loads"             , P6_SEGMENT_REG_LOADS             , */
	{67, 67, "cycles", "clocks_processor_is_not_halted", 0x79, 0},	/* P6_CPU_CLK_UNHALTED              , "cpu_clk_unhalted"              , P6_CPU_CLK_UNHALTED              , */
	{67, 67, str_nil, str_nil, 0x79, 0}
};

  /* K7 (i686) Athlon */

#define K7_NUMEVENTS 57
event_t K7_event[K7_NUMEVENTS + 1] = {
	{0, 0, str_nil, "Data cache accesses", 0x40, 0},	/* K7_DATA_MEM_REFS                   , "data_mem_refs"                   ,  0 ,  2 , K7_DATA_CACHE_ACCESSES                                        , */
	{1, 1, str_nil, "Data cache misses", 0x41, 0},	/* K7_DCU_LINES_IN                    , "dcu_lines_in"                    ,  1 ,  3 , K7_DATA_CACHE_MISSES                                          , */
	{2, 2, "L1_data_misses", "Data cache refills from L2", 0x42, 0x1F},	/* K7_L1_MISSES                       , "L1_misses"                       ,  2 ,  4 , K7_DATA_CACHE_REFILLS                                         , */
	{3, 3, "L2_data_misses", "Data cache refills from system", 0x43, 0x1F},	/* K7_L2_MISSES                       , "L2_misses"                       ,  3 ,  5 , K7_DATA_CACHE_REFILLS_FROM_SYSTEM                             , */
	{4, 4, str_nil, "Data cache writebacks", 0x44, 0x1F},	/* K7_DCU_WRITEBACKS                  , "dcu_writebacks"                  ,  4 ,  6 , K7_DATA_CACHE_WRITEBACKS                                      , */
	{5, 5, "TLB_misses", "L1 DTLB misses and L2 DTLB hits", 0x45, 0},	/* K7_TLB1_MISSES_PROPER              , "TLB1_misses_proper"              ,  5 ,  7 , K7_L1_DTLB_MISSES_AND_L2_DTLB_HITS                            , */
	{6, 6, str_nil, "L1 and L2 DTLB misses", 0x46, 0},	/* K7_TLB2_MISSES                     , "TLB2_misses"                     ,  6 ,  8 , K7_L1_AND_L2_DTLB_MISSES                                      , */
	{7, 7, str_nil, "Misaligned data references", 0x47, 0},	/* K7_MISALIGN_MEM_REF                , "misalign_mem_ref"                ,  7 ,  9 , K7_MISALIGNED_DATA_REFERENCES                                 , */
	{8, 8, str_nil, "Instruction cache fetches", 0x80, 0},	/* K7_IFU_IFETCH                      , "ifu_ifetch"                      ,  8 , 18 , K7_INSTRUCTION_CACHE_FETCHES                                  , */
	{9, 9, str_nil, "Instruction cache misses", 0x81, 0},	/* K7_IFU_IFETCH_MISS                 , "ifu_ifetch_miss"                 ,  9 , 19 , K7_INSTRUCTION_CACHE_MISSES                                   , */
	{10, 10, "iTLB_misses", "L1 ITLB misses (and L2 ITLB hits)", 0x84, 0},	/* K7_ITLB1_MISSES_PROPER             , "ITLB1_misses_proper"             , 10 , 22 , K7_L1_ITLB_MISSES                                             , */
	{11, 11, str_nil, "(L1 and) L2 ITLB misses", 0x85, 0},	/* K7_ITLB2_MISSES                    , "ITLB2_misses"                    , 11 , 23 , K7_L2_ITLB_MISSES                                             , */
	{12, 12, str_nil, "Retired instructions (includes exceptions, interrupts, resyncs)", 0xC0, 0},	/* K7_INST_RETIRED                    , "inst_retired"                    , 12 , 28 , K7_RETIRED_INSTRUCTIONS                                       , */
	{13, 13, str_nil, "Retired Ops", 0xC1, 0},	/* K7_UOPS_RETIRED                    , "uops_retired"                    , 13 , 29 , K7_RETIRED_OPS                                                , */
	{14, 14, "branches", "Retired branches (conditional, unconditional, exceptions, interrupts)", 0xC2, 0},	/* K7_BR_INST_RETIRED                 , "br_inst_retired"                 , 14 , 30 , K7_RETIRED_BRANCHES                                           , */
	{15, 15, "branch_misses", "Retired branches mispredicted", 0xC3, 0},	/* K7_BR_MISS_PRED_RETIRED            , "br_miss_pred_retired"            , 15 , 31 , K7_RETIRED_BRANCHES_MISPREDICTED                              , */
	{16, 16, "Tbranches", "Retired taken branches", 0xC4, 0},	/* K7_BR_TAKEN_RETIRED                , "br_taken_retired"                , 16 , 32 , K7_RETIRED_TAKEN_BRANCHES                                     , */
	{17, 17, "Tbranch_misses", "Retired taken branches mispredicted", 0xC5, 0},	/* K7_BR_MISS_PRED_TAKEN_RET          , "br_miss_pred_taken_ret"          , 17 , 33 , K7_RETIRED_TAKEN_BRANCHES_MISPREDICTED                        , */
	{18, 18, str_nil, "Retired far control transfers", 0xC6, 0},	/* K7_FAR_CTRTF_RETIRED               , "far_ctrtf_retired"               , 18 , 34 , K7_RETIRED_FAR_CONTROL_TRANSFERS                              , */
	{19, 19, str_nil, "Retired resync branches (only non-control transfer branches counted)", 0xC7, 0},	/* K7_BR_RESYNC_RETIRED               , "br_resync_retired"               , 19 , 35 , K7_RETIRED_RESYNC_BRANCHES                                    , */
	{20, 20, str_nil, "Interrupts masked cycles (IF=0)", 0xCD, 0},	/* K7_CYCLES_INT_MASKED               , "cycles_int_masked"               , 20 , 39 , K7_INTERRUPTS_MASKED_CYCLES                                   , */
	{21, 21, str_nil, "Number of taken hardware interrupts", 0xCF, 0},	/* K7_HW_INT_RX                       , "hw_int_rx"                       , 21 , 41 , K7_NUMBER_OF_TAKEN_HARDWARE_INTERRUPTS                        , */
	{22, 22, str_nil, "Segment register loads", 0x20, 0x3F},	/* K7_seg_reg_loads                   , "seg_reg_loads"                   , 22 ,  0 , K7_SEGMENT_REGISTER_LOADS                                     , */
	{23, 23, str_nil, "Stores to active instruction stream", 0x21, 0},	/* K7_store_to_act_instr_stream       , "store_to_act_instr_stream"       , 23 ,  1 , K7_STORES_TO_ACTIVE_INSTRUCTION_STREAM                        , */
	{24, 24, str_nil, "DRAM system requests", 0x64, 0},	/* K7_dram_sys_req                    , "dram_sys_req"                    , 24 , 10 , K7_DRAM_SYSTEM_REQUESTS                                       , */
	{25, 25, str_nil, "System requests with the selected type", 0x65, 0x73},	/* K7_sys_req_type                    , "sys_req_type"                    , 25 , 11 , K7_SYSTEM_REQUESTS_WITH_THE_SELECTED_TYPE                     , */
	{26, 26, str_nil, "Snoop hits", 0x73, 0x7},	/* K7_snoop_hits                      , "snoop_hits"                      , 26 , 12 , K7_SNOOP_HITS                                                 , */
	{27, 27, str_nil, "Single bit ECC errors detected or corrected", 0x74, 0x3},	/* K7_ecc_errors                      , "ecc_errors"                      , 27 , 13 , K7_SINGLE_BIT_ECC_ERRORS_DETECTED_OR_CORRECTED                , */
	{28, 28, str_nil, "Internal cache line invalidates", 0x75, 0xF},	/* K7_cache_line_invalid              , "cache_line_invalid"              , 28 , 14 , K7_INTERNAL_CACHE_LINE_INVALIDATES                            , */
	{29, 29, "cycles", "Cycles processor is running", 0x76, 0},	/* K7_cyc_cpu_running                 , "cyc_cpu_running"                 , 29 , 15 , K7_CYCLES_PROCESSOR_IS_RUNNING                                , */
	{30, 30, str_nil, "L2 requests", 0x79, 0xFF},	/* K7_L2_requests                     , "L2_requests"                     , 30 , 16 , K7_L2_REQUESTS                                                , */
	{31, 31, str_nil, "Cycles that at least one fill request waited to use the L2", 0x7A, 0},	/* K7_cyc_fill_stall                  , "cyc_fill_stall"                  , 31 , 17 , K7_CYCLES_THAT_AT_LEAST_ONE_FILL_REQUEST_WAITED_TO_USE_THE_L2 , */
	{32, 32, str_nil, "Snoop resyncs", 0x86, 0},	/* K7_snoop_resyncs                   , "snoop_resyncs"                   , 32 , 24 , K7_SNOOP_RESYNCS                                              , */
	{33, 33, str_nil, "Instruction fetch stall cycles", 0x87, 0},	/* K7_instr_fetch_stall               , "instr_fetch_stall"               , 33 , 25 , K7_INSTRUCTION_FETCH_STALL_CYCLES                             , */
	{34, 34, str_nil, "Return stack hits", 0x88, 0},	/* K7_rtrn_stack_hits                 , "rtrn_stack_hits"                 , 34 , 26 , K7_RETURN_STACK_HITS                                          , */
	{35, 35, str_nil, "Return stack overflow", 0x89, 0},	/* K7_rtrn_stack_overflow             , "rtrn_stack_overflow"             , 35 , 27 , K7_RETURN_STACK_OVERFLOW                                      , */
	{36, 36, str_nil, "Retired near returns", 0xC8, 0},	/* K7_near_rtrn_retired               , "near_rtrn_retired"               , 36 , 36 , K7_RETIRED_NEAR_RETURNS                                       , */
	{37, 37, str_nil, "Retired near returns mispredicted", 0xC9, 0},	/* K7_near_rtrn_miss_pred_retired     , "near_rtrn_miss_pred_retired"     , 37 , 37 , K7_RETIRED_NEAR_RETURNS_MISPREDICTED                          , */
	{38, 38, str_nil, "Retired indirect branches with target mispredicted", 0xCA, 0},	/* K7_ind_br_target_miss_pred_retired , "ind_br_target_miss_pred_retired" , 38 , 38 , K7_RETIRED_INDIRECT_BRANCHES_WITH_TARGET_MISPREDICTED         , */
	{39, 39, str_nil, "Interrupts asked while pending cycles", 0xCE, 0},	/* K7_cyc_int_masked_pending          , "cyc_int_masked_pending"          , 39 , 40 , K7_INTERRUPTS_MASKED_WHILE_PENDING_CYCLES                     , */
	{40, 40, str_nil, "Instruction decoder empty", 0xD0, 0},	/* K7_instr_dec_empty                 , "instr_dec_empty"                 , 40 , 42 , K7_INSTRUCTION_DECODER_EMPTY                                  , */
	{41, 41, str_nil, "Dispatch stalls", 0xD1, 0},	/* K7_dispatch_stall                  , "dispatch_stall"                  , 41 , 43 , K7_DISPATCH_STALLS                                            , */
	{42, 42, str_nil, "Branch aborts to retire", 0xD2, 0},	/* K7_br_aborts_retire                , "br_aborts_retire"                , 42 , 44 , K7_BRANCH_ABORTS_TO_RETIRE                                    , */
	{43, 43, str_nil, "Serialize", 0xD3, 0},	/* K7_serialize                       , "serialize"                       , 43 , 45 , K7_SERIALIZE                                                  , */
	{44, 44, str_nil, "Segment load stall", 0xD4, 0},	/* K7_seg_load_stall                  , "seg_load_stall"                  , 44 , 46 , K7_SEGMENT_LOAD_STALL                                         , */
	{45, 45, str_nil, "ICU full", 0xD5, 0},	/* K7_ICU_full                        , "ICU_full"                        , 45 , 47 , K7_ICU_FULL                                                   , */
	{46, 46, str_nil, "Reservation stations full", 0xD6, 0},	/* K7_res_stations_full               , "res_stations_full"               , 46 , 48 , K7_RESERVATION_STATIONS_FULL                                  , */
	{47, 47, str_nil, "FPU full", 0xD7, 0},	/* K7_FPU_full                        , "FPU_full"                        , 47 , 49 , K7_FPU_FULL                                                   , */
	{48, 48, str_nil, "LS full", 0xD8, 0},	/* K7_LS_full                         , "LS_full"                         , 48 , 50 , K7_LS_FULL                                                    , */
	{49, 49, str_nil, "All quiet stall", 0xD9, 0},	/* K7_all_quiet_stall                 , "all_quiet_stall"                 , 49 , 51 , K7_ALL_QUIET_STALL                                            , */
	{50, 50, str_nil, "Far transfer or resync branch pending", 0xDA, 0},	/* K7_far_tf_rs_br_pending            , "far_tf_rs_br_pending"            , 50 , 52 , K7_FAR_TRANSFER_OR_RESYNC_BRANCH_PENDING                      , */
	{51, 51, str_nil, "Breakpoint matches for DR0", 0xDC, 0},	/* K7_brk_pnt_DR0                     , "brk_pnt_DR0"                     , 51 , 53 , K7_BREAKPOINT_MATCHES_FOR_DR0                                 , */
	{52, 52, str_nil, "Breakpoint matches for DR1", 0xDD, 0},	/* K7_brk_pnt_DR1                     , "brk_pnt_DR1"                     , 52 , 54 , K7_BREAKPOINT_MATCHES_FOR_DR1                                 , */
	{53, 53, str_nil, "Breakpoint matches for DR2", 0xDE, 0},	/* K7_brk_pnt_DR2                     , "brk_pnt_DR2"                     , 53 , 55 , K7_BREAKPOINT_MATCHES_FOR_DR2                                 , */
	{54, 54, str_nil, "Breakpoint matches for DR3", 0xDF, 0},	/* K7_brk_pnt_DR3                     , "brk_pnt_DR3"                     , 54 , 56 , K7_BREAKPOINT_MATCHES_FOR_DR3                                 , */
	{55, 55, "L1_inst_misses", "Instruction cache refills from L2", 0x82, 0},	/* 0                                  , "IC_REFILLS_FROM_L2"              , -1 , 20 , K7_INSTRUCTION_CACHE_REFILLS_FROM_L2                          , */
	{56, 56, "L2_inst_misses", "Instruction cache refills from System", 0x83, 0},	/* 0                                  , "IC_REFILLS_FROM_SYSTEM"          , -1 , 21 , K7_INSTRUCTION_CACHE_REFILLS_FROM_SYSTEM                      , */
	{29, 29, str_nil, str_nil, 0x76, 0}
};

  /* K8 (x86_64) Opteron */

#define K8_NUMEVENTS 79
event_t K8_event[K8_NUMEVENTS + 1] = {
	{ 0,  0, str_nil, "K7_DATA_CACHE_ACCESSES", 0x40, 0},
	{ 1,  1, str_nil, "K7_DATA_CACHE_MISSES", 0x41, 0},
	{ 2,  2, "L1_data_misses", "K7_DATA_CACHE_REFILLS_FROM_L2", 0x42, 0},
	{ 3,  3, "L2_data_misses", "K7_DATA_CACHE_REFILLS_FROM_SYSTEM", 0x43, 0},
	{ 4,  4, str_nil, "K7_DATA_CACHE_WRITEBACKS", 0x44, 0},
	{ 5,  5, "TLB_misses",   "K7_L1_DTLB_MISSES_AND_L2_DTLB_HITS", 0x45, 0},
	{ 6,  6, str_nil, "K7_L1_AND_L2_DTLB_MISSES", 0x46, 0},
	{ 7,  7, str_nil, "K7_MISALIGNED_DATA_REFERENCES", 0x47, 0},
	{ 8,  8, str_nil, "K7_INSTRUCTION_CACHE_FETCHES", 0x80, 0},
	{ 9,  9, str_nil, "K7_INSTRUCTION_CACHE_MISSES", 0x81, 0},
	{10, 10, "iTLB_misses", "K7_L1_ITLB_MISSES_AND_L2_ITLB_HITS", 0x84, 0},
	{11, 11, str_nil, "K7_L1_AND_L2_ITLB_MISSES", 0x85, 0},
	{12, 12, str_nil, "K7_RETIRED_INSTRUCTIONS", 0xC0, 0},
	{13, 13, str_nil, "K7_RETIRED_OPS", 0xC1, 0},
	{14, 14, "branches", "K7_RETIRED_BRANCHES", 0xC2, 0},
	{15, 15, "branch_misses", "K7_RETIRED_BRANCHES_MISPREDICTED", 0xC3, 0},
	{16, 16, "Tbranches", "K7_RETIRED_TAKEN_BRANCHES", 0xC4, 0},
	{17, 17, "Tbranch_misses", "K7_RETIRED_TAKEN_BRANCHES_MISPREDICTED", 0xC5, 0},
	{18, 18, str_nil, "K7_RETIRED_FAR_CONTROL_TRANSFERS", 0xC6, 0},
	{19, 19, str_nil, "K7_RETIRED_RESYNC_BRANCHES", 0xC7, 0},
	{20, 20, str_nil, "K7_INTERRUPTS_MASKED_CYCLES", 0xCD, 0},
	{21, 21, str_nil, "K7_INTERRUPTS_MASKED_WHILE_PENDING_CYCLES", 0xCE, 0},
	{22, 22, str_nil, "K7_NUMBER_OF_TAKEN_HARDWARE_INTERRUPTS", 0xCF, 0},
	{23, 23, str_nil, "K8_DISPATCHED_FPU_OPS", 0x00, 0},
	{24, 24, str_nil, "K8_NO_FPU_OPS", 0x01, 0},
	{25, 25, str_nil, "K8_FAST_FPU_OPS", 0x02, 0},
	{26, 26, str_nil, "K8_SEG_REG_LOAD", 0x20, 0},
	{27, 27, str_nil, "K8_SELF_MODIFY_RESYNC", 0x21, 0},
	{28, 28, str_nil, "K8_LS_RESYNC_BY_SNOOP", 0x22, 0},
	{29, 29, str_nil, "K8_LS_BUFFER_FULL", 0x23, 0},
	{30, 30, str_nil, "K8_OP_LATE_CANCEL", 0x25, 0},
	{31, 31, str_nil, "K8_CFLUSH_RETIRED", 0x26, 0},
	{32, 32, str_nil, "K8_CPUID_RETIRED", 0x27, 0},
	{33, 33, str_nil, "K8_ACCESS_CANCEL_LATE", 0x48, 0},
	{34, 34, str_nil, "K8_ACCESS_CANCEL_EARLY", 0x49, 0},
	{35, 35, str_nil, "K8_ECC_BIT_ERR", 0x4A, 0},
	{36, 36, str_nil, "K8_DISPATCHED_PRE_INSTRS", 0x4B, 0},
	{37, 37, "cycles", "K8_CPU_CLK_UNHALTED", 0x76, 0},
	{38, 38, str_nil, "K8_BU_INT_L2_REQ", 0x7D, 0},
	{39, 39, str_nil, "K8_BU_FILL_REQ", 0x7E, 0},
	{40, 40, str_nil, "K8_BU_FILL_L2", 0x7F, 0},
	{41, 41, "L1_inst_misses", "K8_IC_REFILL_FROM_L2", 0x82, 0},
	{42, 42, "L2_inst_misses", "K8_IC_REFILL_FROM_SYS", 0x83, 0},
	{43, 43, str_nil, "K8_IC_RESYNC_BY_SNOOP", 0x86, 0},
	{44, 44, str_nil, "K8_IC_FETCH_STALL", 0x87, 0},
	{45, 45, str_nil, "K8_IC_STACK_HIT", 0x88, 0},
	{46, 46, str_nil, "K8_IC_STACK_OVERFLOW", 0x89, 0},
	{47, 47, str_nil, "K8_RETIRED_NEAR_RETURNS", 0xC8, 0},
	{48, 48, str_nil, "K8_RETIRED_RETURNS_MISPREDICT", 0xC9, 0},
	{49, 49, str_nil, "K8_RETIRED_BRANCH_MISCOMPARE", 0xCA, 0},
	{50, 50, str_nil, "K8_RETIRED_FPU_INSTRS", 0xCB, 0},
	{51, 51, str_nil, "K8_RETIRED_FASTPATH_INSTRS", 0xCC, 0},
	{52, 52, str_nil, "K8_DECODER_EMPTY", 0xD0, 0},
	{53, 53, str_nil, "K8_DISPATCH_STALLS", 0xD1, 0},
	{54, 54, str_nil, "K8_DISPATCH_STALL_FROM_BRANCH_ABORT", 0xD2, 0},
	{55, 55, str_nil, "K8_DISPATCH_STALL_SERIALIZATION", 0xD3, 0},
	{56, 56, str_nil, "K8_DISPATCH_STALL_SEG_LOAD", 0xD4, 0},
	{57, 57, str_nil, "K8_DISPATCH_STALL_REORDER_BUFFER", 0xD5, 0},
	{58, 58, str_nil, "K8_DISPATCH_STALL_RESERVE_STATIONS", 0xD6, 0},
	{59, 59, str_nil, "K8_DISPATCH_STALL_FPU", 0xD7, 0},
	{60, 60, str_nil, "K8_DISPATCH_STALL_LS", 0xD8, 0},
	{61, 61, str_nil, "K8_DISPATCH_STALL_QUIET_WAIT", 0xD9, 0},
	{62, 62, str_nil, "K8_DISPATCH_STALL_PENDING", 0xDA, 0},
	{63, 63, str_nil, "K8_FPU_EXCEPTIONS", 0xDB, 0},
	{64, 64, str_nil, "K8_DR0_BREAKPOINTS", 0xDC, 0},
	{65, 65, str_nil, "K8_DR1_BREAKPOINTS", 0xDD, 0},
	{66, 66, str_nil, "K8_DR2_BREAKPOINTS", 0xDE, 0},
	{67, 67, str_nil, "K8_DR3_BREAKPOINTS", 0xDF, 0},
	{68, 68, str_nil, "K8_MEM_PAGE_ACCESS", 0xE0, 0},
	{69, 69, str_nil, "K8_MEM_PAGE_TBL_OVERFLOW", 0xE1, 0},
	{70, 70, str_nil, "K8_DRAM_SLOTS_MISSED", 0xE2, 0},
	{71, 71, str_nil, "K8_MEM_TURNAROUND", 0xE3, 0},
	{72, 72, str_nil, "K8_MEM_BYPASS_SAT", 0xE4, 0},
	{73, 73, str_nil, "K8_SIZED_COMMANDS", 0xEB, 0},
	{74, 74, str_nil, "K8_PROBE_RESULT", 0xEC, 0},
	{75, 75, str_nil, "K8_HYPERTRANSPORT_BUS0_WIDTH", 0xF6, 0},
	{76, 76, str_nil, "K8_HYPERTRANSPORT_BUS1_WIDTH", 0xF7, 0},
	{77, 77, str_nil, "K8_HYPERTRANSPORT_BUS2_WIDTH", 0xF8, 0},
	{78, 78, str_nil, "K8_LOCKED_OP", 0x24, 0},
	{37, 37, str_nil, str_nil, 0x76, 0}
};

/* P4 */
#define P4_NUMEVENTS 49
event_t P4_event[P4_NUMEVENTS + 1] = {
	/* default pefctr configurations */
	{ 0, 0, str_nil, "P4_TC_DELIVER_MODE", 0, 0 },
	{ 1, 1, str_nil, "P4_BPU_FETCH_REQUEST", 1, 0 },
	{ 2, 2, str_nil, "P4_ITLB_REFERENCE", 2, 0 },
	{ 3, 3, str_nil, "P4_MEMORY_CANCEL", 3, 0 },
	{ 4, 4, str_nil, "P4_MEMORY_COMPLETE", 4, 0 },
	{ 5, 5, str_nil, "P4_LOAD_PORT_REPLAY", 5, 0 },
	{ 6, 6, str_nil, "P4_STORE_PORT_REPLAY", 6, 0 },
	{ 7, 7, str_nil, "P4_MOB_LOAD_REPLAY", 7, 0 },
	{ 8, 8, str_nil, "P4_PAGE_WALK_TYPE", 8, 0 },
	{ 9, 9, str_nil, "P4_BSQ_CACHE_REFERENCE", 9, 0 },
	{ 10, 10, str_nil, "P4_IOQ_ALLOCATION", 10, 0 },
	{ 11, 11, str_nil, "P4_IOQ_ACTIVE_ENTRIES", 11, 0 },
	{ 12, 12, str_nil, "P4_FSB_DATA_ACTIVITY", 12, 0 },
	{ 13, 13, str_nil, "P4_BSQ_ALLOCATION", 13, 0 },
	{ 14, 14, str_nil, "P4_BSQ_ACTIVE_ENTRIES", 14, 0 },
	{ 15, 15, str_nil, "P4_SSE_INPUT_ASSIST", 15, 0 },
	{ 16, 16, str_nil, "P4_PACKED_SP_UOP", 16, 0 },
	{ 17, 17, str_nil, "P4_PACKED_DP_UOP", 17, 0 },
	{ 18, 18, str_nil, "P4_SCALAR_SP_UOP", 18, 0 },
	{ 19, 19, str_nil, "P4_SCALAR_DP_UOP", 19, 0 },
	{ 20, 20, str_nil, "P4_64BIT_MMX_UOP", 20, 0 },
	{ 21, 21, str_nil, "P4_128BIT_MMX_UOP", 21, 0 },
	{ 22, 22, str_nil, "P4_X87_FP_UOP", 22, 0 },
	{ 23, 23, str_nil, "P4_X87_SIMD_MOVES_UOP", 23, 0 },
	{ 24, 24, str_nil, "P4_TC_MISC", 24, 0 },
	{ 25, 25, str_nil, "P4_GLOBAL_POWER_EVENTS", 25, 0 },
	{ 26, 26, str_nil, "P4_TC_MS_XFER", 26, 0 },
	{ 27, 27, str_nil, "P4_UOP_QUEUE_WRITES", 27, 0 },
	{ 28, 28, str_nil, "P4_RETIRED_MISPRED_BRANCH_TYPE", 28, 0 },
	{ 29, 29, str_nil, "P4_RETIRED_BRANCH_TYPE", 29, 0 },
	{ 30, 30, str_nil, "P4_RESOURCE_STALL", 30, 0 },
	{ 31, 31, str_nil, "P4_WC_BUFFER", 31, 0 },
	{ 32, 32, str_nil, "P4_B2B_CYCLES", 32, 0 },
	{ 33, 33, str_nil, "P4_BNR", 33, 0 },
	{ 34, 34, str_nil, "P4_SNOOP", 34, 0 },
	{ 35, 35, str_nil, "P4_RESPONSE", 35, 0 },
	{ 36, 36, str_nil, "P4_FRONT_END_EVENT", 36, 0 },
	{ 37, 37, str_nil, "P4_EXECUTION_EVENT", 37, 0 },
	{ 38, 38, str_nil, "P4_REPLAY_EVENT", 38, 0 },
	{ 39, 39, str_nil, "P4_INSTR_RETIRED", 39, 0 },
	{ 40, 40, str_nil, "P4_UOPS_RETIRED", 40, 0 },
	{ 41, 41, str_nil, "P4_UOP_TYPE", 41, 0 },
	{ 42, 42, str_nil, "P4_BRANCH_RETIRED", 42, 0 },
	{ 43, 43, str_nil, "P4_MISPRED_BRANCH_RETIRED", 43, 0 },
	{ 44, 44, str_nil, "P4_X87_ASSIST", 44, 0 },
	{ 45, 45, str_nil, "P4_MACHINE_CLEAR", 45, 0 },
	{ 46, 46, str_nil, "P4M3_INSTR_COMPLETED", 46, 0 },
	/* customized events (inspired by pcl library code ;)
	 * (NOTE: the P4_ names are not official, but made up by me, sandor) */
	{ 47, 47, "Load/Store Instructions", "P4_LOAD_STORE", 12, 0x100 },
	{ 48, 48, "L2 Cache Miss", "P4_L2_CACHE_MISS", 8, 0x1 },
	{ 49, 49, str_nil, str_nil, 49, 0}
};

#if defined(HAVE_LIBPERFCTR)
static int perfctr_event_set_count( const struct perfctr_event_set * s )
{
	int cnt = 0;

	if (s->include)
		cnt = perfctr_event_set_count(s->include);
	cnt += s->nevents;
	return cnt;
}

static const struct perfctr_event * perfctr_event_set_find( const struct perfctr_event_set * s, int cnt, int nr )
{
	cnt -= s->nevents;
	if (s->include && cnt >= nr)
		return perfctr_event_set_find(s->include, cnt, nr);
	return s->events+nr;
}

enum escr_set {
	ALF_ESCR_0_1 = 0,	/* CCCR 12/13/14/15/16/17 via ESCR select 0x01 */
	BPU_ESCR_0_1,	/* CCCR 0/1/2/3 via ESCR select 0x00 */
	BSU_ESCR_0_1,	/* CCCR 0/1/2/3 via ESCR select 0x07 */
	BSU_ESCR_0,		/* CCCR 0/1 via ESCR select 0x07 */
	BSU_ESCR_1,		/* CCCR 2/3 via ESCR select 0x07 */
	CRU_ESCR_0_1,	/* CCCR 12/13/14/15/16/17 via ESCR select 0x04 */
	CRU_ESCR_2_3,	/* CCCR 12/13/14/15/16/17 via ESCR select 0x05 */
	DAC_ESCR_0_1,	/* CCCR 8/9/10/11 via ESCR select 0x05 */
	FIRM_ESCR_0_1,	/* CCCR 8/9/10/11 via ESCR select 0x01 */
	FSB_ESCR_0_1,	/* CCCR 0/1/2/3 via ESCR select 0x06 */
	FSB_ESCR_0,		/* CCCR 0/1 via ESCR select 0x06 */
	FSB_ESCR_1,		/* CCCR 2/3 via ESCR select 0x06 */
	ITLB_ESCR_0_1,	/* CCCR 0/1/2/3 via ESCR select 0x03 */
	MOB_ESCR_0_1,	/* CCCR 0/1/2/3 via ESCR select 0x02 */
	MS_ESCR_0_1,	/* CCCR 4/5/6/7 via ESCR select 0x00 */
	PMH_ESCR_0_1,	/* CCCR 0/1/2/3 via ESCR select 0x04 */
	RAT_ESCR_0_1,	/* CCCR 12/13/14/15/16/17 via ESCR select 0x02 */
	SAAT_ESCR_0_1,	/* CCCR 8/9/10/11 via ESCR select 0x02 */
	TBPU_ESCR_0_1,	/* CCCR 4/5/6/7 via ESCR select 0x02 */
	TC_ESCR_0_1,	/* CCCR 4/5/6/7 via ESCR select 0x01 */
};

int _2ESCR(int cset, int ctr /* 0/1 */) {
	(void)ctr;
	switch(cset) {
		case ALF_ESCR_0_1: return 0x01;
		case BPU_ESCR_0_1: return 0x00;
		case BSU_ESCR_0_1: return 0x07;
		case BSU_ESCR_0: return 0x07;
		case BSU_ESCR_1: return 0x07;
		case CRU_ESCR_0_1: return 0x04;
		case CRU_ESCR_2_3: return 0x05;
		case DAC_ESCR_0_1: return 0x05;
		case FIRM_ESCR_0_1: return 0x01;
		case FSB_ESCR_0_1: return 0x06;
		case FSB_ESCR_0: return 0x06;
		case FSB_ESCR_1: return 0x06;
		case ITLB_ESCR_0_1: return 0x03;
		case MOB_ESCR_0_1: return 0x02;
		case MS_ESCR_0_1: return 0x00;
		case PMH_ESCR_0_1: return 0x04;
		case RAT_ESCR_0_1: return 0x02;
		case SAAT_ESCR_0_1: return 0x02;
		case TBPU_ESCR_0_1: return 0x02;
		case TC_ESCR_0_1: return 0x01;
	}
	return 0;
}

int _2PMC(int cset, int ctr /* 0/1 */) {
	switch(cset) {
		case ALF_ESCR_0_1: return 12 + ctr;
		case BPU_ESCR_0_1:
		case BSU_ESCR_0_1: return 2 + ctr;
		case BSU_ESCR_0: return 0 + ctr;
		case BSU_ESCR_1: return 2 + ctr;
		case CRU_ESCR_0_1: return 14 + ctr;
		case CRU_ESCR_2_3: return 16 + ctr;
		case DAC_ESCR_0_1: return 8 + ctr;
		case FIRM_ESCR_0_1: return 10 + ctr;
		case FSB_ESCR_0_1: return 0 + ctr;
		case FSB_ESCR_0: return 0 + ctr;
		case FSB_ESCR_1: return 2 + ctr;
		case ITLB_ESCR_0_1: return 0 + ctr;
		case MOB_ESCR_0_1: return 2 + ctr;
		case MS_ESCR_0_1: return 4 + ctr;
		case PMH_ESCR_0_1: return 0 + ctr;
		case RAT_ESCR_0_1: return 12 + ctr;
		case SAAT_ESCR_0_1: return 8 + ctr;
		case TBPU_ESCR_0_1: return 6 + ctr;
		case TC_ESCR_0_1: return 4 + ctr;
	}
	return 0;
}

unsigned int
do_event_number
	(unsigned int n, int ctr, struct perfctr_cpu_control *cpu_control)
{
	/* argument n indexes our local p4 table,
	 * translate to perfctr code and mask */
	int code = event[n].code ;
	int mask = event[n].mask ;
	const struct perfctr_event_set *s =
		perfctr_cpu_event_set(PERFCTR_X86_INTEL_P4M3);
	int cnt = perfctr_event_set_count(s);
	const struct perfctr_event *e = perfctr_event_set_find(s, cnt, code);

	if( !s || code >= cnt) {
		fprintf(stderr, "perfex: too many event specifiers\n");
		exit(1);
	}

	/* for now just 0 or 1 */
	cpu_control->evntsel[ctr] = 3 << 16 | 1 << 12
			      | _2ESCR(e->counters_set, ctr)<<13;
	if (mask) {
		cpu_control->p4.escr[ctr] = 1 << 2 /* count the app not the kernel */
			| mask << 9
			| e->evntsel << 25;
	} else {
		cpu_control->p4.escr[ctr] = 1 << 2 /* count the app not the kernel */
			| e->unit_mask->default_value << 9
			| e->evntsel << 25;
	}
	cpu_control->pmc_map[ctr] = 1 << 31 | _2PMC(e->counters_set, ctr);
	cpu_control->nractrs ++;
	cpu_control->tsc_on = 1;

	return n;
}
#endif

#elif ( defined(HW_Linux) && defined(HW_ia64) )

#if defined(HAVE_LIBPFM)
#include <perfmon/pfmlib.h>
#if PFMLIB_MAJ_VERSION(PFMLIB_VERSION) > 2
#include <perfmon/perfmon.h>

typedef struct {
	int fd ;
	pfmlib_input_param_t inp ;
	pfarg_reg_t pd[PFMLIB_MAX_PMDS] ;
	/* these are not actually needed at stop_counters... */
	pfmlib_output_param_t outp ;
	pfarg_reg_t pc[PFMLIB_MAX_PMCS] ;
	pfarg_load_t load_args ;
} pfmInfo_t;

#else

typedef struct {
	pid_t pid;
	pfmlib_param_t evt;
	pfarg_reg_t pd[PMU_MAX_PMDS];
} pfmInfo_t;
#endif
#endif /* HAVE_LIBPFM */

#define X_NUMEVENTS 0
event_t *X_event = NO_event;

/* unfortunately, pfmlib version 3 changed api AND counter definitions... */
#if PFMLIB_MAJ_VERSION(PFMLIB_VERSION) > 2

#define I1_NUMEVENTS 230
event_t I1_event[I1_NUMEVENTS + 1] = {
{0, 0, "PME_ITA_ALAT_INST_CHKA_LDC_ALL", "ALAT_INST_CHKA_LDC_ALL"},  /* { "ALAT_INST_CHKA_LDC_ALL", {0x30036} , 0xf0, 2, {0xffff0003}, NULL}, */
{1, 1, "PME_ITA_ALAT_INST_CHKA_LDC_FP", "ALAT_INST_CHKA_LDC_FP"},  /* { "ALAT_INST_CHKA_LDC_FP", {0x10036} , 0xf0, 2, {0xffff0003}, NULL}, */
{2, 2, "PME_ITA_ALAT_INST_CHKA_LDC_INT", "ALAT_INST_CHKA_LDC_INT"},  /* { "ALAT_INST_CHKA_LDC_INT", {0x20036} , 0xf0, 2, {0xffff0003}, NULL}, */
{3, 3, "PME_ITA_ALAT_INST_FAILED_CHKA_LDC_ALL", "ALAT_INST_FAILED_CHKA_LDC_ALL"},  /* { "ALAT_INST_FAILED_CHKA_LDC_ALL", {0x30037} , 0xf0, 2, {0xffff0003}, NULL}, */
{4, 4, "PME_ITA_ALAT_INST_FAILED_CHKA_LDC_FP", "ALAT_INST_FAILED_CHKA_LDC_FP"},  /* { "ALAT_INST_FAILED_CHKA_LDC_FP", {0x10037} , 0xf0, 2, {0xffff0003}, NULL}, */
{5, 5, "PME_ITA_ALAT_INST_FAILED_CHKA_LDC_INT", "ALAT_INST_FAILED_CHKA_LDC_INT"},  /* { "ALAT_INST_FAILED_CHKA_LDC_INT", {0x20037} , 0xf0, 2, {0xffff0003}, NULL}, */
{6, 6, "PME_ITA_ALAT_REPLACEMENT_ALL", "ALAT_REPLACEMENT_ALL"},  /* { "ALAT_REPLACEMENT_ALL", {0x30038} , 0xf0, 2, {0xffff0007}, NULL}, */
{7, 7, "PME_ITA_ALAT_REPLACEMENT_FP", "ALAT_REPLACEMENT_FP"},  /* { "ALAT_REPLACEMENT_FP", {0x10038} , 0xf0, 2, {0xffff0007}, NULL}, */
{8, 8, "PME_ITA_ALAT_REPLACEMENT_INT", "ALAT_REPLACEMENT_INT"},  /* { "ALAT_REPLACEMENT_INT", {0x20038} , 0xf0, 2, {0xffff0007}, NULL}, */
{9, 9, "PME_ITA_ALL_STOPS_DISPERSED", "ALL_STOPS_DISPERSED"},  /* { "ALL_STOPS_DISPERSED", {0x2f} , 0xf0, 1, {0xffff0001}, NULL}, */
{10, 10, "PME_ITA_BRANCH_EVENT", "BRANCH_EVENT"},  /* { "BRANCH_EVENT", {0x811} , 0xf0, 1, {0xffff0003}, NULL}, */
{11, 11, "PME_ITA_BRANCH_MULTIWAY_ALL_PATHS_ALL_PREDICTIONS", "BRANCH_MULTIWAY_ALL_PATHS_ALL_PREDICTIONS"},  /* { "BRANCH_MULTIWAY_ALL_PATHS_ALL_PREDICTIONS", {0xe} , 0xf0, 1, {0xffff0003}, NULL}, */
{12, 12, "PME_ITA_BRANCH_MULTIWAY_ALL_PATHS_CORRECT_PREDICTIONS", "BRANCH_MULTIWAY_ALL_PATHS_CORRECT_PREDICTIONS"},  /* { "BRANCH_MULTIWAY_ALL_PATHS_CORRECT_PREDICTIONS", {0x1000e} , 0xf0, 1, {0xffff0003}, NULL}, */
{13, 13, "PME_ITA_BRANCH_MULTIWAY_ALL_PATHS_WRONG_PATH", "BRANCH_MULTIWAY_ALL_PATHS_WRONG_PATH"},  /* { "BRANCH_MULTIWAY_ALL_PATHS_WRONG_PATH", {0x2000e} , 0xf0, 1, {0xffff0003}, NULL}, */
{14, 14, "PME_ITA_BRANCH_MULTIWAY_ALL_PATHS_WRONG_TARGET", "BRANCH_MULTIWAY_ALL_PATHS_WRONG_TARGET"},  /* { "BRANCH_MULTIWAY_ALL_PATHS_WRONG_TARGET", {0x3000e} , 0xf0, 1, {0xffff0003}, NULL}, */
{15, 15, "PME_ITA_BRANCH_MULTIWAY_NOT_TAKEN_ALL_PREDICTIONS", "BRANCH_MULTIWAY_NOT_TAKEN_ALL_PREDICTIONS"},  /* { "BRANCH_MULTIWAY_NOT_TAKEN_ALL_PREDICTIONS", {0x8000e} , 0xf0, 1, {0xffff0003}, NULL}, */
{16, 16, "PME_ITA_BRANCH_MULTIWAY_NOT_TAKEN_CORRECT_PREDICTIONS", "BRANCH_MULTIWAY_NOT_TAKEN_CORRECT_PREDICTIONS"},  /* { "BRANCH_MULTIWAY_NOT_TAKEN_CORRECT_PREDICTIONS", {0x9000e} , 0xf0, 1, {0xffff0003}, NULL}, */
{17, 17, "PME_ITA_BRANCH_MULTIWAY_NOT_TAKEN_WRONG_PATH", "BRANCH_MULTIWAY_NOT_TAKEN_WRONG_PATH"},  /* { "BRANCH_MULTIWAY_NOT_TAKEN_WRONG_PATH", {0xa000e} , 0xf0, 1, {0xffff0003}, NULL}, */
{18, 18, "PME_ITA_BRANCH_MULTIWAY_NOT_TAKEN_WRONG_TARGET", "BRANCH_MULTIWAY_NOT_TAKEN_WRONG_TARGET"},  /* { "BRANCH_MULTIWAY_NOT_TAKEN_WRONG_TARGET", {0xb000e} , 0xf0, 1, {0xffff0003}, NULL}, */
{19, 19, "PME_ITA_BRANCH_MULTIWAY_TAKEN_ALL_PREDICTIONS", "BRANCH_MULTIWAY_TAKEN_ALL_PREDICTIONS"},  /* { "BRANCH_MULTIWAY_TAKEN_ALL_PREDICTIONS", {0xc000e} , 0xf0, 1, {0xffff0003}, NULL}, */
{20, 20, "PME_ITA_BRANCH_MULTIWAY_TAKEN_CORRECT_PREDICTIONS", "BRANCH_MULTIWAY_TAKEN_CORRECT_PREDICTIONS"},  /* { "BRANCH_MULTIWAY_TAKEN_CORRECT_PREDICTIONS", {0xd000e} , 0xf0, 1, {0xffff0003}, NULL}, */
{21, 21, "PME_ITA_BRANCH_MULTIWAY_TAKEN_WRONG_PATH", "BRANCH_MULTIWAY_TAKEN_WRONG_PATH"},  /* { "BRANCH_MULTIWAY_TAKEN_WRONG_PATH", {0xe000e} , 0xf0, 1, {0xffff0003}, NULL}, */
{22, 22, "PME_ITA_BRANCH_MULTIWAY_TAKEN_WRONG_TARGET", "BRANCH_MULTIWAY_TAKEN_WRONG_TARGET"},  /* { "BRANCH_MULTIWAY_TAKEN_WRONG_TARGET", {0xf000e} , 0xf0, 1, {0xffff0003}, NULL}, */
{23, 23, "PME_ITA_BRANCH_NOT_TAKEN", "BRANCH_NOT_TAKEN"},  /* { "BRANCH_NOT_TAKEN", {0x8000d} , 0xf0, 1, {0xffff0003}, NULL}, */
{24, 24, "PME_ITA_BRANCH_PATH_1ST_STAGE_NT_OUTCOMES_CORRECTLY_PREDICTED", "BRANCH_PATH_1ST_STAGE_NT_OUTCOMES_CORRECTLY_PREDICTED"},  /* { "BRANCH_PATH_1ST_STAGE_NT_OUTCOMES_CORRECTLY_PREDICTED", {0x6000f} , 0xf0, 1, {0xffff0003}, NULL}, */
{25, 25, "PME_ITA_BRANCH_PATH_1ST_STAGE_NT_OUTCOMES_INCORRECTLY_PREDICTED", "BRANCH_PATH_1ST_STAGE_NT_OUTCOMES_INCORRECTLY_PREDICTED"},  /* { "BRANCH_PATH_1ST_STAGE_NT_OUTCOMES_INCORRECTLY_PREDICTED", {0x4000f} , 0xf0, 1, {0xffff0003}, NULL}, */
{26, 26, "PME_ITA_BRANCH_PATH_1ST_STAGE_TK_OUTCOMES_CORRECTLY_PREDICTED", "BRANCH_PATH_1ST_STAGE_TK_OUTCOMES_CORRECTLY_PREDICTED"},  /* { "BRANCH_PATH_1ST_STAGE_TK_OUTCOMES_CORRECTLY_PREDICTED", {0x7000f} , 0xf0, 1, {0xffff0003}, NULL}, */
{27, 27, "PME_ITA_BRANCH_PATH_1ST_STAGE_TK_OUTCOMES_INCORRECTLY_PREDICTED", "BRANCH_PATH_1ST_STAGE_TK_OUTCOMES_INCORRECTLY_PREDICTED"},  /* { "BRANCH_PATH_1ST_STAGE_TK_OUTCOMES_INCORRECTLY_PREDICTED", {0x5000f} , 0xf0, 1, {0xffff0003}, NULL}, */
{28, 28, "PME_ITA_BRANCH_PATH_2ND_STAGE_NT_OUTCOMES_CORRECTLY_PREDICTED", "BRANCH_PATH_2ND_STAGE_NT_OUTCOMES_CORRECTLY_PREDICTED"},  /* { "BRANCH_PATH_2ND_STAGE_NT_OUTCOMES_CORRECTLY_PREDICTED", {0xa000f} , 0xf0, 1, {0xffff0003}, NULL}, */
{29, 29, "PME_ITA_BRANCH_PATH_2ND_STAGE_NT_OUTCOMES_INCORRECTLY_PREDICTED", "BRANCH_PATH_2ND_STAGE_NT_OUTCOMES_INCORRECTLY_PREDICTED"},  /* { "BRANCH_PATH_2ND_STAGE_NT_OUTCOMES_INCORRECTLY_PREDICTED", {0x8000f} , 0xf0, 1, {0xffff0003}, NULL}, */
{30, 30, "PME_ITA_BRANCH_PATH_2ND_STAGE_TK_OUTCOMES_CORRECTLY_PREDICTED", "BRANCH_PATH_2ND_STAGE_TK_OUTCOMES_CORRECTLY_PREDICTED"},  /* { "BRANCH_PATH_2ND_STAGE_TK_OUTCOMES_CORRECTLY_PREDICTED", {0xb000f} , 0xf0, 1, {0xffff0003}, NULL}, */
{31, 31, "PME_ITA_BRANCH_PATH_2ND_STAGE_TK_OUTCOMES_INCORRECTLY_PREDICTED", "BRANCH_PATH_2ND_STAGE_TK_OUTCOMES_INCORRECTLY_PREDICTED"},  /* { "BRANCH_PATH_2ND_STAGE_TK_OUTCOMES_INCORRECTLY_PREDICTED", {0x9000f} , 0xf0, 1, {0xffff0003}, NULL}, */
{32, 32, "PME_ITA_BRANCH_PATH_3RD_STAGE_NT_OUTCOMES_CORRECTLY_PREDICTED", "BRANCH_PATH_3RD_STAGE_NT_OUTCOMES_CORRECTLY_PREDICTED"},  /* { "BRANCH_PATH_3RD_STAGE_NT_OUTCOMES_CORRECTLY_PREDICTED", {0xe000f} , 0xf0, 1, {0xffff0003}, NULL}, */
{33, 33, "PME_ITA_BRANCH_PATH_3RD_STAGE_NT_OUTCOMES_INCORRECTLY_PREDICTED", "BRANCH_PATH_3RD_STAGE_NT_OUTCOMES_INCORRECTLY_PREDICTED"},  /* { "BRANCH_PATH_3RD_STAGE_NT_OUTCOMES_INCORRECTLY_PREDICTED", {0xc000f} , 0xf0, 1, {0xffff0003}, NULL}, */
{34, 34, "PME_ITA_BRANCH_PATH_3RD_STAGE_TK_OUTCOMES_CORRECTLY_PREDICTED", "BRANCH_PATH_3RD_STAGE_TK_OUTCOMES_CORRECTLY_PREDICTED"},  /* { "BRANCH_PATH_3RD_STAGE_TK_OUTCOMES_CORRECTLY_PREDICTED", {0xf000f} , 0xf0, 1, {0xffff0003}, NULL}, */
{35, 35, "PME_ITA_BRANCH_PATH_3RD_STAGE_TK_OUTCOMES_INCORRECTLY_PREDICTED", "BRANCH_PATH_3RD_STAGE_TK_OUTCOMES_INCORRECTLY_PREDICTED"},  /* { "BRANCH_PATH_3RD_STAGE_TK_OUTCOMES_INCORRECTLY_PREDICTED", {0xd000f} , 0xf0, 1, {0xffff0003}, NULL}, */
{36, 36, "PME_ITA_BRANCH_PATH_ALL_NT_OUTCOMES_CORRECTLY_PREDICTED", "BRANCH_PATH_ALL_NT_OUTCOMES_CORRECTLY_PREDICTED"},  /* { "BRANCH_PATH_ALL_NT_OUTCOMES_CORRECTLY_PREDICTED", {0x2000f} , 0xf0, 1, {0xffff0003}, NULL}, */
{37, 37, "PME_ITA_BRANCH_PATH_ALL_NT_OUTCOMES_INCORRECTLY_PREDICTED", "BRANCH_PATH_ALL_NT_OUTCOMES_INCORRECTLY_PREDICTED"},  /* { "BRANCH_PATH_ALL_NT_OUTCOMES_INCORRECTLY_PREDICTED", {0xf} , 0xf0, 1, {0xffff0003}, NULL}, */
{38, 38, "PME_ITA_BRANCH_PATH_ALL_TK_OUTCOMES_CORRECTLY_PREDICTED", "BRANCH_PATH_ALL_TK_OUTCOMES_CORRECTLY_PREDICTED"},  /* { "BRANCH_PATH_ALL_TK_OUTCOMES_CORRECTLY_PREDICTED", {0x3000f} , 0xf0, 1, {0xffff0003}, NULL}, */
{39, 39, "PME_ITA_BRANCH_PATH_ALL_TK_OUTCOMES_INCORRECTLY_PREDICTED", "BRANCH_PATH_ALL_TK_OUTCOMES_INCORRECTLY_PREDICTED"},  /* { "BRANCH_PATH_ALL_TK_OUTCOMES_INCORRECTLY_PREDICTED", {0x1000f} , 0xf0, 1, {0xffff0003}, NULL}, */
{40, 40, "PME_ITA_BRANCH_PREDICTOR_1ST_STAGE_ALL_PREDICTIONS", "BRANCH_PREDICTOR_1ST_STAGE_ALL_PREDICTIONS"},  /* { "BRANCH_PREDICTOR_1ST_STAGE_ALL_PREDICTIONS", {0x40010} , 0xf0, 1, {0xffff0003}, NULL}, */
{41, 41, "PME_ITA_BRANCH_PREDICTOR_1ST_STAGE_CORRECT_PREDICTIONS", "BRANCH_PREDICTOR_1ST_STAGE_CORRECT_PREDICTIONS"},  /* { "BRANCH_PREDICTOR_1ST_STAGE_CORRECT_PREDICTIONS", {0x50010} , 0xf0, 1, {0xffff0003}, NULL}, */
{42, 42, "PME_ITA_BRANCH_PREDICTOR_1ST_STAGE_WRONG_PATH", "BRANCH_PREDICTOR_1ST_STAGE_WRONG_PATH"},  /* { "BRANCH_PREDICTOR_1ST_STAGE_WRONG_PATH", {0x60010} , 0xf0, 1, {0xffff0003}, NULL}, */
{43, 43, "PME_ITA_BRANCH_PREDICTOR_1ST_STAGE_WRONG_TARGET", "BRANCH_PREDICTOR_1ST_STAGE_WRONG_TARGET"},  /* { "BRANCH_PREDICTOR_1ST_STAGE_WRONG_TARGET", {0x70010} , 0xf0, 1, {0xffff0003}, NULL}, */
{44, 44, "PME_ITA_BRANCH_PREDICTOR_2ND_STAGE_ALL_PREDICTIONS", "BRANCH_PREDICTOR_2ND_STAGE_ALL_PREDICTIONS"},  /* { "BRANCH_PREDICTOR_2ND_STAGE_ALL_PREDICTIONS", {0x80010} , 0xf0, 1, {0xffff0003}, NULL}, */
{45, 45, "PME_ITA_BRANCH_PREDICTOR_2ND_STAGE_CORRECT_PREDICTIONS", "BRANCH_PREDICTOR_2ND_STAGE_CORRECT_PREDICTIONS"},  /* { "BRANCH_PREDICTOR_2ND_STAGE_CORRECT_PREDICTIONS", {0x90010} , 0xf0, 1, {0xffff0003}, NULL}, */
{46, 46, "PME_ITA_BRANCH_PREDICTOR_2ND_STAGE_WRONG_PATH", "BRANCH_PREDICTOR_2ND_STAGE_WRONG_PATH"},  /* { "BRANCH_PREDICTOR_2ND_STAGE_WRONG_PATH", {0xa0010} , 0xf0, 1, {0xffff0003}, NULL}, */
{47, 47, "PME_ITA_BRANCH_PREDICTOR_2ND_STAGE_WRONG_TARGET", "BRANCH_PREDICTOR_2ND_STAGE_WRONG_TARGET"},  /* { "BRANCH_PREDICTOR_2ND_STAGE_WRONG_TARGET", {0xb0010} , 0xf0, 1, {0xffff0003}, NULL}, */
{48, 48, "PME_ITA_BRANCH_PREDICTOR_3RD_STAGE_ALL_PREDICTIONS", "BRANCH_PREDICTOR_3RD_STAGE_ALL_PREDICTIONS"},  /* { "BRANCH_PREDICTOR_3RD_STAGE_ALL_PREDICTIONS", {0xc0010} , 0xf0, 1, {0xffff0003}, NULL}, */
{49, 49, "PME_ITA_BRANCH_PREDICTOR_3RD_STAGE_CORRECT_PREDICTIONS", "BRANCH_PREDICTOR_3RD_STAGE_CORRECT_PREDICTIONS"},  /* { "BRANCH_PREDICTOR_3RD_STAGE_CORRECT_PREDICTIONS", {0xd0010} , 0xf0, 1, {0xffff0003}, NULL}, */
{50, 50, "PME_ITA_BRANCH_PREDICTOR_3RD_STAGE_WRONG_PATH", "BRANCH_PREDICTOR_3RD_STAGE_WRONG_PATH"},  /* { "BRANCH_PREDICTOR_3RD_STAGE_WRONG_PATH", {0xe0010} , 0xf0, 1, {0xffff0003}, NULL}, */
{51, 51, "PME_ITA_BRANCH_PREDICTOR_3RD_STAGE_WRONG_TARGET", "BRANCH_PREDICTOR_3RD_STAGE_WRONG_TARGET"},  /* { "BRANCH_PREDICTOR_3RD_STAGE_WRONG_TARGET", {0xf0010} , 0xf0, 1, {0xffff0003}, NULL}, */
{52, 52, "PME_ITA_BRANCH_PREDICTOR_ALL_ALL_PREDICTIONS", "BRANCH_PREDICTOR_ALL_ALL_PREDICTIONS"},  /* { "BRANCH_PREDICTOR_ALL_ALL_PREDICTIONS", {0x10} , 0xf0, 1, {0xffff0003}, NULL}, */
{53, 53, "PME_ITA_BRANCH_PREDICTOR_ALL_CORRECT_PREDICTIONS", "BRANCH_PREDICTOR_ALL_CORRECT_PREDICTIONS"},  /* { "BRANCH_PREDICTOR_ALL_CORRECT_PREDICTIONS", {0x10010} , 0xf0, 1, {0xffff0003}, NULL}, */
{54, 54, "PME_ITA_BRANCH_PREDICTOR_ALL_WRONG_PATH", "BRANCH_PREDICTOR_ALL_WRONG_PATH"},  /* { "BRANCH_PREDICTOR_ALL_WRONG_PATH", {0x20010} , 0xf0, 1, {0xffff0003}, NULL}, */
{55, 55, "PME_ITA_BRANCH_PREDICTOR_ALL_WRONG_TARGET", "BRANCH_PREDICTOR_ALL_WRONG_TARGET"},  /* { "BRANCH_PREDICTOR_ALL_WRONG_TARGET", {0x30010} , 0xf0, 1, {0xffff0003}, NULL}, */
{56, 56, "PME_ITA_BRANCH_TAKEN_SLOT_0", "BRANCH_TAKEN_SLOT_0"},  /* { "BRANCH_TAKEN_SLOT_0", {0x1000d} , 0xf0, 1, {0xffff0003}, NULL}, */
{57, 57, "PME_ITA_BRANCH_TAKEN_SLOT_1", "BRANCH_TAKEN_SLOT_1"},  /* { "BRANCH_TAKEN_SLOT_1", {0x2000d} , 0xf0, 1, {0xffff0003}, NULL}, */
{58, 58, "PME_ITA_BRANCH_TAKEN_SLOT_2", "BRANCH_TAKEN_SLOT_2"},  /* { "BRANCH_TAKEN_SLOT_2", {0x4000d} , 0xf0, 1, {0xffff0003}, NULL}, */
{59, 59, "PME_ITA_BUS_ALL_ANY", "BUS_ALL_ANY"},  /* { "BUS_ALL_ANY", {0x10047} , 0xf0, 1, {0xffff0000}, NULL}, */
{60, 60, "PME_ITA_BUS_ALL_IO", "BUS_ALL_IO"},  /* { "BUS_ALL_IO", {0x40047} , 0xf0, 1, {0xffff0000}, NULL}, */
{61, 61, "PME_ITA_BUS_ALL_SELF", "BUS_ALL_SELF"},  /* { "BUS_ALL_SELF", {0x20047} , 0xf0, 1, {0xffff0000}, NULL}, */
{62, 62, "PME_ITA_BUS_BRQ_LIVE_REQ_HI", "BUS_BRQ_LIVE_REQ_HI"},  /* { "BUS_BRQ_LIVE_REQ_HI", {0x5c} , 0xf0, 2, {0xffff0000}, NULL}, */
{63, 63, "PME_ITA_BUS_BRQ_LIVE_REQ_LO", "BUS_BRQ_LIVE_REQ_LO"},  /* { "BUS_BRQ_LIVE_REQ_LO", {0x5b} , 0xf0, 2, {0xffff0000}, NULL}, */
{64, 64, "PME_ITA_BUS_BRQ_REQ_INSERTED", "BUS_BRQ_REQ_INSERTED"},  /* { "BUS_BRQ_REQ_INSERTED", {0x5d} , 0xf0, 1, {0xffff0000}, NULL}, */
{65, 65, "PME_ITA_BUS_BURST_ANY", "BUS_BURST_ANY"},  /* { "BUS_BURST_ANY", {0x10049} , 0xf0, 1, {0xffff0000}, NULL}, */
{66, 66, "PME_ITA_BUS_BURST_IO", "BUS_BURST_IO"},  /* { "BUS_BURST_IO", {0x40049} , 0xf0, 1, {0xffff0000}, NULL}, */
{67, 67, "PME_ITA_BUS_BURST_SELF", "BUS_BURST_SELF"},  /* { "BUS_BURST_SELF", {0x20049} , 0xf0, 1, {0xffff0000}, NULL}, */
{68, 68, "PME_ITA_BUS_HITM", "BUS_HITM"},  /* { "BUS_HITM", {0x44} , 0xf0, 1, {0xffff0000}, NULL}, */
{69, 69, "PME_ITA_BUS_IO_ANY", "BUS_IO_ANY"},  /* { "BUS_IO_ANY", {0x10050} , 0xf0, 1, {0xffff0000}, NULL}, */
{70, 70, "PME_ITA_BUS_IOQ_LIVE_REQ_HI", "BUS_IOQ_LIVE_REQ_HI"},  /* { "BUS_IOQ_LIVE_REQ_HI", {0x58} , 0xf0, 3, {0xffff0000}, NULL}, */
{71, 71, "PME_ITA_BUS_IOQ_LIVE_REQ_LO", "BUS_IOQ_LIVE_REQ_LO"},  /* { "BUS_IOQ_LIVE_REQ_LO", {0x57} , 0xf0, 3, {0xffff0000}, NULL}, */
{72, 72, "PME_ITA_BUS_IO_SELF", "BUS_IO_SELF"},  /* { "BUS_IO_SELF", {0x20050} , 0xf0, 1, {0xffff0000}, NULL}, */
{73, 73, "PME_ITA_BUS_LOCK_ANY", "BUS_LOCK_ANY"},  /* { "BUS_LOCK_ANY", {0x10053} , 0xf0, 1, {0xffff0000}, NULL}, */
{74, 74, "PME_ITA_BUS_LOCK_CYCLES_ANY", "BUS_LOCK_CYCLES_ANY"},  /* { "BUS_LOCK_CYCLES_ANY", {0x10054} , 0xf0, 1, {0xffff0000}, NULL}, */
{75, 75, "PME_ITA_BUS_LOCK_CYCLES_SELF", "BUS_LOCK_CYCLES_SELF"},  /* { "BUS_LOCK_CYCLES_SELF", {0x20054} , 0xf0, 1, {0xffff0000}, NULL}, */
{76, 76, "PME_ITA_BUS_LOCK_SELF", "BUS_LOCK_SELF"},  /* { "BUS_LOCK_SELF", {0x20053} , 0xf0, 1, {0xffff0000}, NULL}, */
{77, 77, "PME_ITA_BUS_MEMORY_ANY", "BUS_MEMORY_ANY"},  /* { "BUS_MEMORY_ANY", {0x1004a} , 0xf0, 1, {0xffff0000}, NULL}, */
{78, 78, "PME_ITA_BUS_MEMORY_IO", "BUS_MEMORY_IO"},  /* { "BUS_MEMORY_IO", {0x4004a} , 0xf0, 1, {0xffff0000}, NULL}, */
{79, 79, "PME_ITA_BUS_MEMORY_SELF", "BUS_MEMORY_SELF"},  /* { "BUS_MEMORY_SELF", {0x2004a} , 0xf0, 1, {0xffff0000}, NULL}, */
{80, 80, "PME_ITA_BUS_PARTIAL_ANY", "BUS_PARTIAL_ANY"},  /* { "BUS_PARTIAL_ANY", {0x10048} , 0xf0, 1, {0xffff0000}, NULL}, */
{81, 81, "PME_ITA_BUS_PARTIAL_IO", "BUS_PARTIAL_IO"},  /* { "BUS_PARTIAL_IO", {0x40048} , 0xf0, 1, {0xffff0000}, NULL}, */
{82, 82, "PME_ITA_BUS_PARTIAL_SELF", "BUS_PARTIAL_SELF"},  /* { "BUS_PARTIAL_SELF", {0x20048} , 0xf0, 1, {0xffff0000}, NULL}, */
{83, 83, "PME_ITA_BUS_RD_ALL_ANY", "BUS_RD_ALL_ANY"},  /* { "BUS_RD_ALL_ANY", {0x1004b} , 0xf0, 1, {0xffff0000}, NULL}, */
{84, 84, "PME_ITA_BUS_RD_ALL_IO", "BUS_RD_ALL_IO"},  /* { "BUS_RD_ALL_IO", {0x4004b} , 0xf0, 1, {0xffff0000}, NULL}, */
{85, 85, "PME_ITA_BUS_RD_ALL_SELF", "BUS_RD_ALL_SELF"},  /* { "BUS_RD_ALL_SELF", {0x2004b} , 0xf0, 1, {0xffff0000}, NULL}, */
{86, 86, "PME_ITA_BUS_RD_DATA_ANY", "BUS_RD_DATA_ANY"},  /* { "BUS_RD_DATA_ANY", {0x1004c} , 0xf0, 1, {0xffff0000}, NULL}, */
{87, 87, "PME_ITA_BUS_RD_DATA_IO", "BUS_RD_DATA_IO"},  /* { "BUS_RD_DATA_IO", {0x4004c} , 0xf0, 1, {0xffff0000}, NULL}, */
{88, 88, "PME_ITA_BUS_RD_DATA_SELF", "BUS_RD_DATA_SELF"},  /* { "BUS_RD_DATA_SELF", {0x2004c} , 0xf0, 1, {0xffff0000}, NULL}, */
{89, 89, "PME_ITA_BUS_RD_HIT", "BUS_RD_HIT"},  /* { "BUS_RD_HIT", {0x40} , 0xf0, 1, {0xffff0000}, NULL}, */
{90, 90, "PME_ITA_BUS_RD_HITM", "BUS_RD_HITM"},  /* { "BUS_RD_HITM", {0x41} , 0xf0, 1, {0xffff0000}, NULL}, */
{91, 91, "PME_ITA_BUS_RD_INVAL_ANY", "BUS_RD_INVAL_ANY"},  /* { "BUS_RD_INVAL_ANY", {0x1004e} , 0xf0, 1, {0xffff0000}, NULL}, */
{92, 92, "PME_ITA_BUS_RD_INVAL_BST_ANY", "BUS_RD_INVAL_BST_ANY"},  /* { "BUS_RD_INVAL_BST_ANY", {0x1004f} , 0xf0, 1, {0xffff0000}, NULL}, */
{93, 93, "PME_ITA_BUS_RD_INVAL_BST_HITM", "BUS_RD_INVAL_BST_HITM"},  /* { "BUS_RD_INVAL_BST_HITM", {0x43} , 0xf0, 1, {0xffff0000}, NULL}, */
{94, 94, "PME_ITA_BUS_RD_INVAL_BST_IO", "BUS_RD_INVAL_BST_IO"},  /* { "BUS_RD_INVAL_BST_IO", {0x4004f} , 0xf0, 1, {0xffff0000}, NULL}, */
{95, 95, "PME_ITA_BUS_RD_INVAL_BST_SELF", "BUS_RD_INVAL_BST_SELF"},  /* { "BUS_RD_INVAL_BST_SELF", {0x2004f} , 0xf0, 1, {0xffff0000}, NULL}, */
{96, 96, "PME_ITA_BUS_RD_INVAL_HITM", "BUS_RD_INVAL_HITM"},  /* { "BUS_RD_INVAL_HITM", {0x42} , 0xf0, 1, {0xffff0000}, NULL}, */
{97, 97, "PME_ITA_BUS_RD_INVAL_IO", "BUS_RD_INVAL_IO"},  /* { "BUS_RD_INVAL_IO", {0x4004e} , 0xf0, 1, {0xffff0000}, NULL}, */
{98, 98, "PME_ITA_BUS_RD_INVAL_SELF", "BUS_RD_INVAL_SELF"},  /* { "BUS_RD_INVAL_SELF", {0x2004e} , 0xf0, 1, {0xffff0000}, NULL}, */
{99, 99, "PME_ITA_BUS_RD_IO_ANY", "BUS_RD_IO_ANY"},  /* { "BUS_RD_IO_ANY", {0x10051} , 0xf0, 1, {0xffff0000}, NULL}, */
{100, 100, "PME_ITA_BUS_RD_IO_SELF", "BUS_RD_IO_SELF"},  /* { "BUS_RD_IO_SELF", {0x20051} , 0xf0, 1, {0xffff0000}, NULL}, */
{101, 101, "PME_ITA_BUS_RD_PRTL_ANY", "BUS_RD_PRTL_ANY"},  /* { "BUS_RD_PRTL_ANY", {0x1004d} , 0xf0, 1, {0xffff0000}, NULL}, */
{102, 102, "PME_ITA_BUS_RD_PRTL_IO", "BUS_RD_PRTL_IO"},  /* { "BUS_RD_PRTL_IO", {0x4004d} , 0xf0, 1, {0xffff0000}, NULL}, */
{103, 103, "PME_ITA_BUS_RD_PRTL_SELF", "BUS_RD_PRTL_SELF"},  /* { "BUS_RD_PRTL_SELF", {0x2004d} , 0xf0, 1, {0xffff0000}, NULL}, */
{104, 104, "PME_ITA_BUS_SNOOPQ_REQ", "BUS_SNOOPQ_REQ"},  /* { "BUS_SNOOPQ_REQ", {0x56} , 0x30, 3, {0xffff0000}, NULL}, */
{105, 105, "PME_ITA_BUS_SNOOPS_ANY", "BUS_SNOOPS_ANY"},  /* { "BUS_SNOOPS_ANY", {0x10046} , 0xf0, 1, {0xffff0000}, NULL}, */
{106, 106, "PME_ITA_BUS_SNOOPS_HITM_ANY", "BUS_SNOOPS_HITM_ANY"},  /* { "BUS_SNOOPS_HITM_ANY", {0x10045} , 0xf0, 1, {0xffff0000}, NULL}, */
{107, 107, "PME_ITA_BUS_SNOOP_STALL_CYCLES_ANY", "BUS_SNOOP_STALL_CYCLES_ANY"},  /* { "BUS_SNOOP_STALL_CYCLES_ANY", {0x10055} , 0xf0, 1, {0xffff0000}, NULL}, */
{108, 108, "PME_ITA_BUS_SNOOP_STALL_CYCLES_SELF", "BUS_SNOOP_STALL_CYCLES_SELF"},  /* { "BUS_SNOOP_STALL_CYCLES_SELF", {0x20055} , 0xf0, 1, {0xffff0000}, NULL}, */
{109, 109, "PME_ITA_BUS_WR_WB_ANY", "BUS_WR_WB_ANY"},  /* { "BUS_WR_WB_ANY", {0x10052} , 0xf0, 1, {0xffff0000}, NULL}, */
{110, 110, "PME_ITA_BUS_WR_WB_IO", "BUS_WR_WB_IO"},  /* { "BUS_WR_WB_IO", {0x40052} , 0xf0, 1, {0xffff0000}, NULL}, */
{111, 111, "PME_ITA_BUS_WR_WB_SELF", "BUS_WR_WB_SELF"},  /* { "BUS_WR_WB_SELF", {0x20052} , 0xf0, 1, {0xffff0000}, NULL}, */
{112, 112, "PME_ITA_CPU_CPL_CHANGES", "CPU_CPL_CHANGES"},  /* { "CPU_CPL_CHANGES", {0x34} , 0xf0, 1, {0xffff0000}, NULL}, */
{113, 113, "PME_ITA_CPU_CYCLES", "CPU_CYCLES"},  /* { "CPU_CYCLES", {0x12} , 0xf0, 1, {0xffff0000}, NULL}, */
{114, 114, "PME_ITA_DATA_ACCESS_CYCLE", "DATA_ACCESS_CYCLE"},  /* { "DATA_ACCESS_CYCLE", {0x3} , 0xf0, 1, {0xffff0000}, NULL}, */
{115, 115, "PME_ITA_DATA_EAR_CACHE_LAT1024", "DATA_EAR_CACHE_LAT1024"},  /* { "DATA_EAR_CACHE_LAT1024", {0x90367} , 0xf0, 1, {0xffff0003}, NULL}, */
{116, 116, "PME_ITA_DATA_EAR_CACHE_LAT128", "DATA_EAR_CACHE_LAT128"},  /* { "DATA_EAR_CACHE_LAT128", {0x50367} , 0xf0, 1, {0xffff0003}, NULL}, */
{117, 117, "PME_ITA_DATA_EAR_CACHE_LAT16", "DATA_EAR_CACHE_LAT16"},  /* { "DATA_EAR_CACHE_LAT16", {0x20367} , 0xf0, 1, {0xffff0003}, NULL}, */
{118, 118, "PME_ITA_DATA_EAR_CACHE_LAT2048", "DATA_EAR_CACHE_LAT2048"},  /* { "DATA_EAR_CACHE_LAT2048", {0xa0367} , 0xf0, 1, {0xffff0003}, NULL}, */
{119, 119, "PME_ITA_DATA_EAR_CACHE_LAT256", "DATA_EAR_CACHE_LAT256"},  /* { "DATA_EAR_CACHE_LAT256", {0x60367} , 0xf0, 1, {0xffff0003}, NULL}, */
{120, 120, "PME_ITA_DATA_EAR_CACHE_LAT32", "DATA_EAR_CACHE_LAT32"},  /* { "DATA_EAR_CACHE_LAT32", {0x30367} , 0xf0, 1, {0xffff0003}, NULL}, */
{121, 121, "PME_ITA_DATA_EAR_CACHE_LAT4", "DATA_EAR_CACHE_LAT4"},  /* { "DATA_EAR_CACHE_LAT4", {0x367} , 0xf0, 1, {0xffff0003}, NULL}, */
{122, 122, "PME_ITA_DATA_EAR_CACHE_LAT512", "DATA_EAR_CACHE_LAT512"},  /* { "DATA_EAR_CACHE_LAT512", {0x80367} , 0xf0, 1, {0xffff0003}, NULL}, */
{123, 123, "PME_ITA_DATA_EAR_CACHE_LAT64", "DATA_EAR_CACHE_LAT64"},  /* { "DATA_EAR_CACHE_LAT64", {0x40367} , 0xf0, 1, {0xffff0003}, NULL}, */
{124, 124, "PME_ITA_DATA_EAR_CACHE_LAT8", "DATA_EAR_CACHE_LAT8"},  /* { "DATA_EAR_CACHE_LAT8", {0x10367} , 0xf0, 1, {0xffff0003}, NULL}, */
{125, 125, "PME_ITA_DATA_EAR_CACHE_LAT_NONE", "DATA_EAR_CACHE_LAT_NONE"},  /* { "DATA_EAR_CACHE_LAT_NONE", {0xf0367} , 0xf0, 1, {0xffff0003}, NULL}, */
{126, 126, "PME_ITA_DATA_EAR_EVENTS", "DATA_EAR_EVENTS"},  /* { "DATA_EAR_EVENTS", {0x67} , 0xf0, 1, {0xffff0007}, NULL}, */
{127, 127, "PME_ITA_DATA_EAR_TLB_L2", "DATA_EAR_TLB_L2"},  /* { "DATA_EAR_TLB_L2", {0x20767} , 0xf0, 1, {0xffff0003}, NULL}, */
{128, 128, "PME_ITA_DATA_EAR_TLB_SW", "DATA_EAR_TLB_SW"},  /* { "DATA_EAR_TLB_SW", {0x80767} , 0xf0, 1, {0xffff0003}, NULL}, */
{129, 129, "PME_ITA_DATA_EAR_TLB_VHPT", "DATA_EAR_TLB_VHPT"},  /* { "DATA_EAR_TLB_VHPT", {0x40767} , 0xf0, 1, {0xffff0003}, NULL}, */
{130, 130, "PME_ITA_DATA_REFERENCES_RETIRED", "DATA_REFERENCES_RETIRED"},  /* { "DATA_REFERENCES_RETIRED", {0x63} , 0xf0, 2, {0xffff0007}, NULL}, */
{131, 131, "PME_ITA_DEPENDENCY_ALL_CYCLE", "DEPENDENCY_ALL_CYCLE"},  /* { "DEPENDENCY_ALL_CYCLE", {0x6} , 0xf0, 1, {0xffff0000}, NULL}, */
{132, 132, "PME_ITA_DEPENDENCY_SCOREBOARD_CYCLE", "DEPENDENCY_SCOREBOARD_CYCLE"},  /* { "DEPENDENCY_SCOREBOARD_CYCLE", {0x2} , 0xf0, 1, {0xffff0000}, NULL}, */
{133, 133, "PME_ITA_DTC_MISSES", "DTC_MISSES"},  /* { "DTC_MISSES", {0x60} , 0xf0, 1, {0xffff0007}, NULL}, */
{134, 134, "PME_ITA_DTLB_INSERTS_HPW", "DTLB_INSERTS_HPW"},  /* { "DTLB_INSERTS_HPW", {0x62} , 0xf0, 1, {0xffff0007}, NULL}, */
{135, 135, "PME_ITA_DTLB_MISSES", "DTLB_MISSES"},  /* { "DTLB_MISSES", {0x61} , 0xf0, 1, {0xffff0007}, NULL}, */
{136, 136, "PME_ITA_EXPL_STOPBITS", "EXPL_STOPBITS"},  /* { "EXPL_STOPBITS", {0x2e} , 0xf0, 1, {0xffff0001}, NULL}, */
{137, 137, "PME_ITA_FP_FLUSH_TO_ZERO", "FP_FLUSH_TO_ZERO"},  /* { "FP_FLUSH_TO_ZERO", {0xb} , 0xf0, 2, {0xffff0003}, NULL}, */
{138, 138, "PME_ITA_FP_OPS_RETIRED_HI", "FP_OPS_RETIRED_HI"},  /* { "FP_OPS_RETIRED_HI", {0xa} , 0xf0, 3, {0xffff0003}, NULL}, */
{139, 139, "PME_ITA_FP_OPS_RETIRED_LO", "FP_OPS_RETIRED_LO"},  /* { "FP_OPS_RETIRED_LO", {0x9} , 0xf0, 3, {0xffff0003}, NULL}, */
{140, 140, "PME_ITA_FP_SIR_FLUSH", "FP_SIR_FLUSH"},  /* { "FP_SIR_FLUSH", {0xc} , 0xf0, 2, {0xffff0003}, NULL}, */
{141, 141, "PME_ITA_IA32_INST_RETIRED", "IA32_INST_RETIRED"},  /* { "IA32_INST_RETIRED", {0x15} , 0xf0, 2, {0xffff0000}, NULL}, */
{142, 142, "PME_ITA_IA64_INST_RETIRED", "IA64_INST_RETIRED"},  /* { "IA64_INST_RETIRED", {0x8} , 0x30, 6, {0xffff0003}, NULL}, */
{143, 143, "PME_ITA_IA64_TAGGED_INST_RETIRED_PMC8", "IA64_TAGGED_INST_RETIRED_PMC8"},  /* { "IA64_TAGGED_INST_RETIRED_PMC8", {0x30008} , 0x30, 6, {0xffff0003}, NULL}, */
{144, 144, "PME_ITA_IA64_TAGGED_INST_RETIRED_PMC9", "IA64_TAGGED_INST_RETIRED_PMC9"},  /* { "IA64_TAGGED_INST_RETIRED_PMC9", {0x20008} , 0x30, 6, {0xffff0003}, NULL}, */
{145, 145, "PME_ITA_INST_ACCESS_CYCLE", "INST_ACCESS_CYCLE"},  /* { "INST_ACCESS_CYCLE", {0x1} , 0xf0, 1, {0xffff0000}, NULL}, */
{146, 146, "PME_ITA_INST_DISPERSED", "INST_DISPERSED"},  /* { "INST_DISPERSED", {0x2d} , 0x30, 6, {0xffff0001}, NULL}, */
{147, 147, "PME_ITA_INST_FAILED_CHKS_RETIRED_ALL", "INST_FAILED_CHKS_RETIRED_ALL"},  /* { "INST_FAILED_CHKS_RETIRED_ALL", {0x30035} , 0xf0, 1, {0xffff0003}, NULL}, */
{148, 148, "PME_ITA_INST_FAILED_CHKS_RETIRED_FP", "INST_FAILED_CHKS_RETIRED_FP"},  /* { "INST_FAILED_CHKS_RETIRED_FP", {0x20035} , 0xf0, 1, {0xffff0003}, NULL}, */
{149, 149, "PME_ITA_INST_FAILED_CHKS_RETIRED_INT", "INST_FAILED_CHKS_RETIRED_INT"},  /* { "INST_FAILED_CHKS_RETIRED_INT", {0x10035} , 0xf0, 1, {0xffff0003}, NULL}, */
{150, 150, "PME_ITA_INSTRUCTION_EAR_CACHE_LAT1024", "INSTRUCTION_EAR_CACHE_LAT1024"},  /* { "INSTRUCTION_EAR_CACHE_LAT1024", {0x80123} , 0xf0, 1, {0xffff0001}, NULL}, */
{151, 151, "PME_ITA_INSTRUCTION_EAR_CACHE_LAT128", "INSTRUCTION_EAR_CACHE_LAT128"},  /* { "INSTRUCTION_EAR_CACHE_LAT128", {0x50123} , 0xf0, 1, {0xffff0001}, NULL}, */
{152, 152, "PME_ITA_INSTRUCTION_EAR_CACHE_LAT16", "INSTRUCTION_EAR_CACHE_LAT16"},  /* { "INSTRUCTION_EAR_CACHE_LAT16", {0x20123} , 0xf0, 1, {0xffff0001}, NULL}, */
{153, 153, "PME_ITA_INSTRUCTION_EAR_CACHE_LAT2048", "INSTRUCTION_EAR_CACHE_LAT2048"},  /* { "INSTRUCTION_EAR_CACHE_LAT2048", {0x90123} , 0xf0, 1, {0xffff0001}, NULL}, */
{154, 154, "PME_ITA_INSTRUCTION_EAR_CACHE_LAT256", "INSTRUCTION_EAR_CACHE_LAT256"},  /* { "INSTRUCTION_EAR_CACHE_LAT256", {0x60123} , 0xf0, 1, {0xffff0001}, NULL}, */
{155, 155, "PME_ITA_INSTRUCTION_EAR_CACHE_LAT32", "INSTRUCTION_EAR_CACHE_LAT32"},  /* { "INSTRUCTION_EAR_CACHE_LAT32", {0x30123} , 0xf0, 1, {0xffff0001}, NULL}, */
{156, 156, "PME_ITA_INSTRUCTION_EAR_CACHE_LAT4096", "INSTRUCTION_EAR_CACHE_LAT4096"},  /* { "INSTRUCTION_EAR_CACHE_LAT4096", {0xa0123} , 0xf0, 1, {0xffff0001}, NULL}, */
{157, 157, "PME_ITA_INSTRUCTION_EAR_CACHE_LAT4", "INSTRUCTION_EAR_CACHE_LAT4"},  /* { "INSTRUCTION_EAR_CACHE_LAT4", {0x123} , 0xf0, 1, {0xffff0001}, NULL}, */
{158, 158, "PME_ITA_INSTRUCTION_EAR_CACHE_LAT512", "INSTRUCTION_EAR_CACHE_LAT512"},  /* { "INSTRUCTION_EAR_CACHE_LAT512", {0x70123} , 0xf0, 1, {0xffff0001}, NULL}, */
{159, 159, "PME_ITA_INSTRUCTION_EAR_CACHE_LAT64", "INSTRUCTION_EAR_CACHE_LAT64"},  /* { "INSTRUCTION_EAR_CACHE_LAT64", {0x40123} , 0xf0, 1, {0xffff0001}, NULL}, */
{160, 160, "PME_ITA_INSTRUCTION_EAR_CACHE_LAT8", "INSTRUCTION_EAR_CACHE_LAT8"},  /* { "INSTRUCTION_EAR_CACHE_LAT8", {0x10123} , 0xf0, 1, {0xffff0001}, NULL}, */
{161, 161, "PME_ITA_INSTRUCTION_EAR_CACHE_LAT_NONE", "INSTRUCTION_EAR_CACHE_LAT_NONE"},  /* { "INSTRUCTION_EAR_CACHE_LAT_NONE", {0xf0123} , 0xf0, 1, {0xffff0001}, NULL}, */
{162, 162, "PME_ITA_INSTRUCTION_EAR_EVENTS", "INSTRUCTION_EAR_EVENTS"},  /* { "INSTRUCTION_EAR_EVENTS", {0x23} , 0xf0, 1, {0xffff0001}, NULL}, */
{163, 163, "PME_ITA_INSTRUCTION_EAR_TLB_SW", "INSTRUCTION_EAR_TLB_SW"},  /* { "INSTRUCTION_EAR_TLB_SW", {0x80523} , 0xf0, 1, {0xffff0001}, NULL}, */
{164, 164, "PME_ITA_INSTRUCTION_EAR_TLB_VHPT", "INSTRUCTION_EAR_TLB_VHPT"},  /* { "INSTRUCTION_EAR_TLB_VHPT", {0x40523} , 0xf0, 1, {0xffff0001}, NULL}, */
{165, 165, "PME_ITA_ISA_TRANSITIONS", "ISA_TRANSITIONS"},  /* { "ISA_TRANSITIONS", {0x14} , 0xf0, 1, {0xffff0000}, NULL}, */
{166, 166, "PME_ITA_ISB_LINES_IN", "ISB_LINES_IN"},  /* { "ISB_LINES_IN", {0x26} , 0xf0, 1, {0xffff0000}, NULL}, */
{167, 167, "PME_ITA_ITLB_INSERTS_HPW", "ITLB_INSERTS_HPW"},  /* { "ITLB_INSERTS_HPW", {0x28} , 0xf0, 1, {0xffff0001}, NULL}, */
{168, 168, "PME_ITA_ITLB_MISSES_FETCH", "ITLB_MISSES_FETCH"},  /* { "ITLB_MISSES_FETCH", {0x27} , 0xf0, 1, {0xffff0001}, NULL}, */
{169, 169, "PME_ITA_L1D_READ_FORCED_MISSES_RETIRED", "L1D_READ_FORCED_MISSES_RETIRED"},  /* { "L1D_READ_FORCED_MISSES_RETIRED", {0x6b} , 0xf0, 2, {0xffff0007}, NULL}, */
{170, 170, "PME_ITA_L1D_READ_MISSES_RETIRED", "L1D_READ_MISSES_RETIRED"},  /* { "L1D_READ_MISSES_RETIRED", {0x66} , 0xf0, 2, {0xffff0007}, NULL}, */
{171, 171, "PME_ITA_L1D_READS_RETIRED", "L1D_READS_RETIRED"},  /* { "L1D_READS_RETIRED", {0x64} , 0xf0, 2, {0xffff0007}, NULL}, */
{172, 172, "PME_ITA_L1I_DEMAND_READS", "L1I_DEMAND_READS"},  /* { "L1I_DEMAND_READS", {0x20} , 0xf0, 1, {0xffff0001}, NULL}, */
{173, 173, "PME_ITA_L1I_FILLS", "L1I_FILLS"},  /* { "L1I_FILLS", {0x21} , 0xf0, 1, {0xffff0000}, NULL}, */
{174, 174, "PME_ITA_L1I_PREFETCH_READS", "L1I_PREFETCH_READS"},  /* { "L1I_PREFETCH_READS", {0x24} , 0xf0, 1, {0xffff0001}, NULL}, */
{175, 175, "PME_ITA_L1_OUTSTANDING_REQ_HI", "L1_OUTSTANDING_REQ_HI"},  /* { "L1_OUTSTANDING_REQ_HI", {0x79} , 0xf0, 1, {0xffff0000}, NULL}, */
{176, 176, "PME_ITA_L1_OUTSTANDING_REQ_LO", "L1_OUTSTANDING_REQ_LO"},  /* { "L1_OUTSTANDING_REQ_LO", {0x78} , 0xf0, 1, {0xffff0000}, NULL}, */
{177, 177, "PME_ITA_L2_DATA_REFERENCES_ALL", "L2_DATA_REFERENCES_ALL"},  /* { "L2_DATA_REFERENCES_ALL", {0x30069} , 0xf0, 2, {0xffff0007}, NULL}, */
{178, 178, "PME_ITA_L2_DATA_REFERENCES_READS", "L2_DATA_REFERENCES_READS"},  /* { "L2_DATA_REFERENCES_READS", {0x10069} , 0xf0, 2, {0xffff0007}, NULL}, */
{179, 179, "PME_ITA_L2_DATA_REFERENCES_WRITES", "L2_DATA_REFERENCES_WRITES"},  /* { "L2_DATA_REFERENCES_WRITES", {0x20069} , 0xf0, 2, {0xffff0007}, NULL}, */
{180, 180, "PME_ITA_L2_FLUSH_DETAILS_ADDR_CONFLICT", "L2_FLUSH_DETAILS_ADDR_CONFLICT"},  /* { "L2_FLUSH_DETAILS_ADDR_CONFLICT", {0x20077} , 0xf0, 1, {0xffff0000}, NULL}, */
{181, 181, "PME_ITA_L2_FLUSH_DETAILS_ALL", "L2_FLUSH_DETAILS_ALL"},  /* { "L2_FLUSH_DETAILS_ALL", {0xf0077} , 0xf0, 1, {0xffff0000}, NULL}, */
{182, 182, "PME_ITA_L2_FLUSH_DETAILS_BUS_REJECT", "L2_FLUSH_DETAILS_BUS_REJECT"},  /* { "L2_FLUSH_DETAILS_BUS_REJECT", {0x40077} , 0xf0, 1, {0xffff0000}, NULL}, */
{183, 183, "PME_ITA_L2_FLUSH_DETAILS_FULL_FLUSH", "L2_FLUSH_DETAILS_FULL_FLUSH"},  /* { "L2_FLUSH_DETAILS_FULL_FLUSH", {0x80077} , 0xf0, 1, {0xffff0000}, NULL}, */
{184, 184, "PME_ITA_L2_FLUSH_DETAILS_ST_BUFFER", "L2_FLUSH_DETAILS_ST_BUFFER"},  /* { "L2_FLUSH_DETAILS_ST_BUFFER", {0x10077} , 0xf0, 1, {0xffff0000}, NULL}, */
{185, 185, "PME_ITA_L2_FLUSHES", "L2_FLUSHES"},  /* { "L2_FLUSHES", {0x76} , 0xf0, 1, {0xffff0000}, NULL}, */
{186, 186, "PME_ITA_L2_INST_DEMAND_READS", "L2_INST_DEMAND_READS"},  /* { "L2_INST_DEMAND_READS", {0x22} , 0xf0, 1, {0xffff0001}, NULL}, */
{187, 187, "PME_ITA_L2_INST_PREFETCH_READS", "L2_INST_PREFETCH_READS"},  /* { "L2_INST_PREFETCH_READS", {0x25} , 0xf0, 1, {0xffff0001}, NULL}, */
{188, 188, "PME_ITA_L2_MISSES", "L2_MISSES"},  /* { "L2_MISSES", {0x6a} , 0xf0, 2, {0xffff0007}, NULL}, */
{189, 189, "PME_ITA_L2_REFERENCES", "L2_REFERENCES"},  /* { "L2_REFERENCES", {0x68} , 0xf0, 3, {0xffff0007}, NULL}, */
{190, 190, "PME_ITA_L3_LINES_REPLACED", "L3_LINES_REPLACED"},  /* { "L3_LINES_REPLACED", {0x7f} , 0xf0, 1, {0xffff0000}, NULL}, */
{191, 191, "PME_ITA_L3_MISSES", "L3_MISSES"},  /* { "L3_MISSES", {0x7c} , 0xf0, 1, {0xffff0000}, NULL}, */
{192, 192, "PME_ITA_L3_READS_ALL_READS_ALL", "L3_READS_ALL_READS_ALL"},  /* { "L3_READS_ALL_READS_ALL", {0xf007d} , 0xf0, 1, {0xffff0000}, NULL}, */
{193, 193, "PME_ITA_L3_READS_ALL_READS_HIT", "L3_READS_ALL_READS_HIT"},  /* { "L3_READS_ALL_READS_HIT", {0xd007d} , 0xf0, 1, {0xffff0000}, NULL}, */
{194, 194, "PME_ITA_L3_READS_ALL_READS_MISS", "L3_READS_ALL_READS_MISS"},  /* { "L3_READS_ALL_READS_MISS", {0xe007d} , 0xf0, 1, {0xffff0000}, NULL}, */
{195, 195, "PME_ITA_L3_READS_DATA_READS_ALL", "L3_READS_DATA_READS_ALL"},  /* { "L3_READS_DATA_READS_ALL", {0xb007d} , 0xf0, 1, {0xffff0000}, NULL}, */
{196, 196, "PME_ITA_L3_READS_DATA_READS_HIT", "L3_READS_DATA_READS_HIT"},  /* { "L3_READS_DATA_READS_HIT", {0x9007d} , 0xf0, 1, {0xffff0000}, NULL}, */
{197, 197, "PME_ITA_L3_READS_DATA_READS_MISS", "L3_READS_DATA_READS_MISS"},  /* { "L3_READS_DATA_READS_MISS", {0xa007d} , 0xf0, 1, {0xffff0000}, NULL}, */
{198, 198, "PME_ITA_L3_READS_INST_READS_ALL", "L3_READS_INST_READS_ALL"},  /* { "L3_READS_INST_READS_ALL", {0x7007d} , 0xf0, 1, {0xffff0000}, NULL}, */
{199, 199, "PME_ITA_L3_READS_INST_READS_HIT", "L3_READS_INST_READS_HIT"},  /* { "L3_READS_INST_READS_HIT", {0x5007d} , 0xf0, 1, {0xffff0000}, NULL}, */
{200, 200, "PME_ITA_L3_READS_INST_READS_MISS", "L3_READS_INST_READS_MISS"},  /* { "L3_READS_INST_READS_MISS", {0x6007d} , 0xf0, 1, {0xffff0000}, NULL}, */
{201, 201, "PME_ITA_L3_REFERENCES", "L3_REFERENCES"},  /* { "L3_REFERENCES", {0x7b} , 0xf0, 1, {0xffff0007}, NULL}, */
{202, 202, "PME_ITA_L3_WRITES_ALL_WRITES_ALL", "L3_WRITES_ALL_WRITES_ALL"},  /* { "L3_WRITES_ALL_WRITES_ALL", {0xf007e} , 0xf0, 1, {0xffff0000}, NULL}, */
{203, 203, "PME_ITA_L3_WRITES_ALL_WRITES_HIT", "L3_WRITES_ALL_WRITES_HIT"},  /* { "L3_WRITES_ALL_WRITES_HIT", {0xd007e} , 0xf0, 1, {0xffff0000}, NULL}, */
{204, 204, "PME_ITA_L3_WRITES_ALL_WRITES_MISS", "L3_WRITES_ALL_WRITES_MISS"},  /* { "L3_WRITES_ALL_WRITES_MISS", {0xe007e} , 0xf0, 1, {0xffff0000}, NULL}, */
{205, 205, "PME_ITA_L3_WRITES_DATA_WRITES_ALL", "L3_WRITES_DATA_WRITES_ALL"},  /* { "L3_WRITES_DATA_WRITES_ALL", {0x7007e} , 0xf0, 1, {0xffff0000}, NULL}, */
{206, 206, "PME_ITA_L3_WRITES_DATA_WRITES_HIT", "L3_WRITES_DATA_WRITES_HIT"},  /* { "L3_WRITES_DATA_WRITES_HIT", {0x5007e} , 0xf0, 1, {0xffff0000}, NULL}, */
{207, 207, "PME_ITA_L3_WRITES_DATA_WRITES_MISS", "L3_WRITES_DATA_WRITES_MISS"},  /* { "L3_WRITES_DATA_WRITES_MISS", {0x6007e} , 0xf0, 1, {0xffff0000}, NULL}, */
{208, 208, "PME_ITA_L3_WRITES_L2_WRITEBACK_ALL", "L3_WRITES_L2_WRITEBACK_ALL"},  /* { "L3_WRITES_L2_WRITEBACK_ALL", {0xb007e} , 0xf0, 1, {0xffff0000}, NULL}, */
{209, 209, "PME_ITA_L3_WRITES_L2_WRITEBACK_HIT", "L3_WRITES_L2_WRITEBACK_HIT"},  /* { "L3_WRITES_L2_WRITEBACK_HIT", {0x9007e} , 0xf0, 1, {0xffff0000}, NULL}, */
{210, 210, "PME_ITA_L3_WRITES_L2_WRITEBACK_MISS", "L3_WRITES_L2_WRITEBACK_MISS"},  /* { "L3_WRITES_L2_WRITEBACK_MISS", {0xa007e} , 0xf0, 1, {0xffff0000}, NULL}, */
{211, 211, "PME_ITA_LOADS_RETIRED", "LOADS_RETIRED"},  /* { "LOADS_RETIRED", {0x6c} , 0xf0, 2, {0xffff0007}, NULL}, */
{212, 212, "PME_ITA_MEMORY_CYCLE", "MEMORY_CYCLE"},  /* { "MEMORY_CYCLE", {0x7} , 0xf0, 1, {0xffff0000}, NULL}, */
{213, 213, "PME_ITA_MISALIGNED_LOADS_RETIRED", "MISALIGNED_LOADS_RETIRED"},  /* { "MISALIGNED_LOADS_RETIRED", {0x70} , 0xf0, 2, {0xffff0007}, NULL}, */
{214, 214, "PME_ITA_MISALIGNED_STORES_RETIRED", "MISALIGNED_STORES_RETIRED"},  /* { "MISALIGNED_STORES_RETIRED", {0x71} , 0xf0, 2, {0xffff0007}, NULL}, */
{215, 215, "PME_ITA_NOPS_RETIRED", "NOPS_RETIRED"},  /* { "NOPS_RETIRED", {0x30} , 0x30, 6, {0xffff0003}, NULL}, */
{216, 216, "PME_ITA_PIPELINE_ALL_FLUSH_CYCLE", "PIPELINE_ALL_FLUSH_CYCLE"},  /* { "PIPELINE_ALL_FLUSH_CYCLE", {0x4} , 0xf0, 1, {0xffff0000}, NULL}, */
{217, 217, "PME_ITA_PIPELINE_BACKEND_FLUSH_CYCLE", "PIPELINE_BACKEND_FLUSH_CYCLE"},  /* { "PIPELINE_BACKEND_FLUSH_CYCLE", {0x0} , 0xf0, 1, {0xffff0000}, NULL}, */
{218, 218, "PME_ITA_PIPELINE_FLUSH_ALL", "PIPELINE_FLUSH_ALL"},  /* { "PIPELINE_FLUSH_ALL", {0xf0033} , 0xf0, 1, {0xffff0000}, NULL}, */
{219, 219, "PME_ITA_PIPELINE_FLUSH_DTC_FLUSH", "PIPELINE_FLUSH_DTC_FLUSH"},  /* { "PIPELINE_FLUSH_DTC_FLUSH", {0x40033} , 0xf0, 1, {0xffff0000}, NULL}, */
{220, 220, "PME_ITA_PIPELINE_FLUSH_IEU_FLUSH", "PIPELINE_FLUSH_IEU_FLUSH"},  /* { "PIPELINE_FLUSH_IEU_FLUSH", {0x80033} , 0xf0, 1, {0xffff0000}, NULL}, */
{221, 221, "PME_ITA_PIPELINE_FLUSH_L1D_WAYMP_FLUSH", "PIPELINE_FLUSH_L1D_WAYMP_FLUSH"},  /* { "PIPELINE_FLUSH_L1D_WAYMP_FLUSH", {0x20033} , 0xf0, 1, {0xffff0000}, NULL}, */
{222, 222, "PME_ITA_PIPELINE_FLUSH_OTHER_FLUSH", "PIPELINE_FLUSH_OTHER_FLUSH"},  /* { "PIPELINE_FLUSH_OTHER_FLUSH", {0x10033} , 0xf0, 1, {0xffff0000}, NULL}, */
{223, 223, "PME_ITA_PREDICATE_SQUASHED_RETIRED", "PREDICATE_SQUASHED_RETIRED"},  /* { "PREDICATE_SQUASHED_RETIRED", {0x31} , 0x30, 6, {0xffff0003}, NULL}, */
{224, 224, "PME_ITA_RSE_LOADS_RETIRED", "RSE_LOADS_RETIRED"},  /* { "RSE_LOADS_RETIRED", {0x72} , 0xf0, 2, {0xffff0007}, NULL}, */
{225, 225, "PME_ITA_RSE_REFERENCES_RETIRED", "RSE_REFERENCES_RETIRED"},  /* { "RSE_REFERENCES_RETIRED", {0x65} , 0xf0, 2, {0xffff0007}, NULL}, */
{226, 226, "PME_ITA_STORES_RETIRED", "STORES_RETIRED"},  /* { "STORES_RETIRED", {0x6d} , 0xf0, 2, {0xffff0007}, NULL}, */
{227, 227, "PME_ITA_UC_LOADS_RETIRED", "UC_LOADS_RETIRED"},  /* { "UC_LOADS_RETIRED", {0x6e} , 0xf0, 2, {0xffff0007}, NULL}, */
{228, 228, "PME_ITA_UC_STORES_RETIRED", "UC_STORES_RETIRED"},  /* { "UC_STORES_RETIRED", {0x6f} , 0xf0, 2, {0xffff0007}, NULL}, */
{229, 229, "PME_ITA_UNSTALLED_BACKEND_CYCLE", "UNSTALLED_BACKEND_CYCLE"},  /* { "UNSTALLED_BACKEND_CYCLE", {0x5} , 0xf0, 1, {0xffff0000}, NULL}}; */
{113, 113, str_nil, str_nil}
};

#define I2_NUMEVENTS 497
event_t I2_event[I2_NUMEVENTS + 1] = {
{0, 0, "PME_ITA2_ALAT_CAPACITY_MISS_ALL", "ALAT_CAPACITY_MISS_ALL"},  /* { "ALAT_CAPACITY_MISS_ALL", {0x30058}, 0xf0, 2, {0xf00007}, "ALAT Entry Replaced -- both integer and floating point instructions"}, */
{1, 1, "PME_ITA2_ALAT_CAPACITY_MISS_FP", "ALAT_CAPACITY_MISS_FP"},  /* { "ALAT_CAPACITY_MISS_FP", {0x20058}, 0xf0, 2, {0xf00007}, "ALAT Entry Replaced -- only floating point instructions"}, */
{2, 2, "PME_ITA2_ALAT_CAPACITY_MISS_INT", "ALAT_CAPACITY_MISS_INT"},  /* { "ALAT_CAPACITY_MISS_INT", {0x10058}, 0xf0, 2, {0xf00007}, "ALAT Entry Replaced -- only integer instructions"}, */
{3, 3, "PME_ITA2_BACK_END_BUBBLE_ALL", "BACK_END_BUBBLE_ALL"},  /* { "BACK_END_BUBBLE_ALL", {0x0}, 0xf0, 1, {0xf00000}, "Full Pipe Bubbles in Main Pipe -- Front-end, RSE, EXE, FPU/L1D stall or a pipeline flush due to an exception/branch misprediction"}, */
{4, 4, "PME_ITA2_BACK_END_BUBBLE_FE", "BACK_END_BUBBLE_FE"},  /* { "BACK_END_BUBBLE_FE", {0x10000}, 0xf0, 1, {0xf00000}, "Full Pipe Bubbles in Main Pipe -- front-end"}, */
{5, 5, "PME_ITA2_BACK_END_BUBBLE_L1D_FPU_RSE", "BACK_END_BUBBLE_L1D_FPU_RSE"},  /* { "BACK_END_BUBBLE_L1D_FPU_RSE", {0x20000}, 0xf0, 1, {0xf00000}, "Full Pipe Bubbles in Main Pipe -- L1D_FPU or RSE."}, */
{6, 6, "PME_ITA2_BE_BR_MISPRED_DETAIL_ANY", "BE_BR_MISPRED_DETAIL_ANY"},  /* { "BE_BR_MISPRED_DETAIL_ANY", {0x61}, 0xf0, 1, {0xf00003}, "BE Branch Misprediction Detail -- any back-end (be) mispredictions"}, */
{7, 7, "PME_ITA2_BE_BR_MISPRED_DETAIL_PFS", "BE_BR_MISPRED_DETAIL_PFS"},  /* { "BE_BR_MISPRED_DETAIL_PFS", {0x30061}, 0xf0, 1, {0xf00003}, "BE Branch Misprediction Detail -- only back-end pfs mispredictions for taken branches"}, */
{8, 8, "PME_ITA2_BE_BR_MISPRED_DETAIL_ROT", "BE_BR_MISPRED_DETAIL_ROT"},  /* { "BE_BR_MISPRED_DETAIL_ROT", {0x20061}, 0xf0, 1, {0xf00003}, "BE Branch Misprediction Detail -- only back-end rotate mispredictions"}, */
{9, 9, "PME_ITA2_BE_BR_MISPRED_DETAIL_STG", "BE_BR_MISPRED_DETAIL_STG"},  /* { "BE_BR_MISPRED_DETAIL_STG", {0x10061}, 0xf0, 1, {0xf00003}, "BE Branch Misprediction Detail -- only back-end stage mispredictions"}, */
{10, 10, "PME_ITA2_BE_EXE_BUBBLE_ALL", "BE_EXE_BUBBLE_ALL"},  /* { "BE_EXE_BUBBLE_ALL", {0x2}, 0xf0, 1, {0xf00000}, "Full Pipe Bubbles in Main Pipe due to Execution Unit Stalls -- Back-end was stalled by exe"}, */
{11, 11, "PME_ITA2_BE_EXE_BUBBLE_ARCR", "BE_EXE_BUBBLE_ARCR"},  /* { "BE_EXE_BUBBLE_ARCR", {0x40002}, 0xf0, 1, {0xf00000}, "Full Pipe Bubbles in Main Pipe due to Execution Unit Stalls -- Back-end was stalled by exe due to AR or CR dependency"}, */
{12, 12, "PME_ITA2_BE_EXE_BUBBLE_ARCR_PR_CANCEL_BANK", "BE_EXE_BUBBLE_ARCR_PR_CANCEL_BANK"},  /* { "BE_EXE_BUBBLE_ARCR_PR_CANCEL_BANK", {0x80002}, 0xf0, 1, {0xf00000}, "Full Pipe Bubbles in Main Pipe due to Execution Unit Stalls -- ARCR, PR, CANCEL or BANK_SWITCH"}, */
{13, 13, "PME_ITA2_BE_EXE_BUBBLE_BANK_SWITCH", "BE_EXE_BUBBLE_BANK_SWITCH"},  /* { "BE_EXE_BUBBLE_BANK_SWITCH", {0x70002}, 0xf0, 1, {0xf00000}, "Full Pipe Bubbles in Main Pipe due to Execution Unit Stalls -- Back-end was stalled by exe due to bank switching."}, */
{14, 14, "PME_ITA2_BE_EXE_BUBBLE_CANCEL", "BE_EXE_BUBBLE_CANCEL"},  /* { "BE_EXE_BUBBLE_CANCEL", {0x60002}, 0xf0, 1, {0xf00000}, "Full Pipe Bubbles in Main Pipe due to Execution Unit Stalls -- Back-end was stalled by exe due to a canceled load"}, */
{15, 15, "PME_ITA2_BE_EXE_BUBBLE_FRALL", "BE_EXE_BUBBLE_FRALL"},  /* { "BE_EXE_BUBBLE_FRALL", {0x20002}, 0xf0, 1, {0xf00000}, "Full Pipe Bubbles in Main Pipe due to Execution Unit Stalls -- Back-end was stalled by exe due to FR/FR or FR/load dependency"}, */
{16, 16, "PME_ITA2_BE_EXE_BUBBLE_GRALL", "BE_EXE_BUBBLE_GRALL"},  /* { "BE_EXE_BUBBLE_GRALL", {0x10002}, 0xf0, 1, {0xf00000}, "Full Pipe Bubbles in Main Pipe due to Execution Unit Stalls -- Back-end was stalled by exe due to GR/GR or GR/load dependency"}, */
{17, 17, "PME_ITA2_BE_EXE_BUBBLE_GRGR", "BE_EXE_BUBBLE_GRGR"},  /* { "BE_EXE_BUBBLE_GRGR", {0x50002}, 0xf0, 1, {0xf00000}, "Full Pipe Bubbles in Main Pipe due to Execution Unit Stalls -- Back-end was stalled by exe due to GR/GR dependency"}, */
{18, 18, "PME_ITA2_BE_EXE_BUBBLE_PR", "BE_EXE_BUBBLE_PR"},  /* { "BE_EXE_BUBBLE_PR", {0x30002}, 0xf0, 1, {0xf00000}, "Full Pipe Bubbles in Main Pipe due to Execution Unit Stalls -- Back-end was stalled by exe due to PR dependency"}, */
{19, 19, "PME_ITA2_BE_FLUSH_BUBBLE_ALL", "BE_FLUSH_BUBBLE_ALL"},  /* { "BE_FLUSH_BUBBLE_ALL", {0x4}, 0xf0, 1, {0xf00000}, "Full Pipe Bubbles in Main Pipe due to Flushes. -- Back-end was stalled due to either an exception/interruption or branch misprediction flush"}, */
{20, 20, "PME_ITA2_BE_FLUSH_BUBBLE_BRU", "BE_FLUSH_BUBBLE_BRU"},  /* { "BE_FLUSH_BUBBLE_BRU", {0x10004}, 0xf0, 1, {0xf00000}, "Full Pipe Bubbles in Main Pipe due to Flushes. -- Back-end was stalled due to a branch misprediction flush"}, */
{21, 21, "PME_ITA2_BE_FLUSH_BUBBLE_XPN", "BE_FLUSH_BUBBLE_XPN"},  /* { "BE_FLUSH_BUBBLE_XPN", {0x20004}, 0xf0, 1, {0xf00000}, "Full Pipe Bubbles in Main Pipe due to Flushes. -- Back-end was stalled due to an exception/interruption flush"}, */
{22, 22, "PME_ITA2_BE_L1D_FPU_BUBBLE_ALL", "BE_L1D_FPU_BUBBLE_ALL"},  /* { "BE_L1D_FPU_BUBBLE_ALL", {0xca}, 0xf0, 1, {0x5210000}, "Full Pipe Bubbles in Main Pipe due to FPU or L1D Cache -- Back-end was stalled by L1D or FPU"}, */
{23, 23, "PME_ITA2_BE_L1D_FPU_BUBBLE_FPU", "BE_L1D_FPU_BUBBLE_FPU"},  /* { "BE_L1D_FPU_BUBBLE_FPU", {0x100ca}, 0xf0, 1, {0x5210000}, "Full Pipe Bubbles in Main Pipe due to FPU or L1D Cache -- Back-end was stalled by FPU."}, */
{24, 24, "PME_ITA2_BE_L1D_FPU_BUBBLE_L1D", "BE_L1D_FPU_BUBBLE_L1D"},  /* { "BE_L1D_FPU_BUBBLE_L1D", {0x200ca}, 0xf0, 1, {0x5210000}, "Full Pipe Bubbles in Main Pipe due to FPU or L1D Cache -- Back-end was stalled by L1D. This includes all stalls caused by the L1 pipeline (created in the L1D stage of the L1 pipeline which corresponds to the DET stage of the main pipe)."}, */
{25, 25, "PME_ITA2_BE_L1D_FPU_BUBBLE_L1D_DCS", "BE_L1D_FPU_BUBBLE_L1D_DCS"},  /* { "BE_L1D_FPU_BUBBLE_L1D_DCS", {0x800ca}, 0xf0, 1, {0x5210000}, "Full Pipe Bubbles in Main Pipe due to FPU or L1D Cache -- Back-end was stalled by L1D due to DCS requiring a stall"}, */
{26, 26, "PME_ITA2_BE_L1D_FPU_BUBBLE_L1D_DCURECIR", "BE_L1D_FPU_BUBBLE_L1D_DCURECIR"},  /* { "BE_L1D_FPU_BUBBLE_L1D_DCURECIR", {0x400ca}, 0xf0, 1, {0x5210000}, "Full Pipe Bubbles in Main Pipe due to FPU or L1D Cache -- Back-end was stalled by L1D due to DCU recirculating"}, */
{27, 27, "PME_ITA2_BE_L1D_FPU_BUBBLE_L1D_FILLCONF", "BE_L1D_FPU_BUBBLE_L1D_FILLCONF"},  /* { "BE_L1D_FPU_BUBBLE_L1D_FILLCONF", {0x700ca}, 0xf0, 1, {0x5210000}, "Full Pipe Bubbles in Main Pipe due to FPU or L1D Cache -- Back-end was stalled by L1D due a store in conflict with a returning fill."}, */
{28, 28, "PME_ITA2_BE_L1D_FPU_BUBBLE_L1D_FULLSTBUF", "BE_L1D_FPU_BUBBLE_L1D_FULLSTBUF"},  /* { "BE_L1D_FPU_BUBBLE_L1D_FULLSTBUF", {0x300ca}, 0xf0, 1, {0x5210000}, "Full Pipe Bubbles in Main Pipe due to FPU or L1D Cache -- Back-end was stalled by L1D due to store buffer being full"}, */
{29, 29, "PME_ITA2_BE_L1D_FPU_BUBBLE_L1D_HPW", "BE_L1D_FPU_BUBBLE_L1D_HPW"},  /* { "BE_L1D_FPU_BUBBLE_L1D_HPW", {0x500ca}, 0xf0, 1, {0x5210000}, "Full Pipe Bubbles in Main Pipe due to FPU or L1D Cache -- Back-end was stalled by L1D due to Hardware Page Walker"}, */
{30, 30, "PME_ITA2_BE_L1D_FPU_BUBBLE_L1D_L2BPRESS", "BE_L1D_FPU_BUBBLE_L1D_L2BPRESS"},  /* { "BE_L1D_FPU_BUBBLE_L1D_L2BPRESS", {0x900ca}, 0xf0, 1, {0x5210000}, "Full Pipe Bubbles in Main Pipe due to FPU or L1D Cache -- Back-end was stalled by L1D due to L2 Back Pressure"}, */
{31, 31, "PME_ITA2_BE_L1D_FPU_BUBBLE_L1D_LDCHK", "BE_L1D_FPU_BUBBLE_L1D_LDCHK"},  /* { "BE_L1D_FPU_BUBBLE_L1D_LDCHK", {0xc00ca}, 0xf0, 1, {0x5210000}, "Full Pipe Bubbles in Main Pipe due to FPU or L1D Cache -- Back-end was stalled by L1D due to architectural ordering conflict"}, */
{32, 32, "PME_ITA2_BE_L1D_FPU_BUBBLE_L1D_LDCONF", "BE_L1D_FPU_BUBBLE_L1D_LDCONF"},  /* { "BE_L1D_FPU_BUBBLE_L1D_LDCONF", {0xb00ca}, 0xf0, 1, {0x5210000}, "Full Pipe Bubbles in Main Pipe due to FPU or L1D Cache -- Back-end was stalled by L1D due to architectural ordering conflict"}, */
{33, 33, "PME_ITA2_BE_L1D_FPU_BUBBLE_L1D_NAT", "BE_L1D_FPU_BUBBLE_L1D_NAT"},  /* { "BE_L1D_FPU_BUBBLE_L1D_NAT", {0xd00ca}, 0xf0, 1, {0x5210000}, "Full Pipe Bubbles in Main Pipe due to FPU or L1D Cache -- Back-end was stalled by L1D due to L1D data return needing recirculated NaT generation."}, */
{34, 34, "PME_ITA2_BE_L1D_FPU_BUBBLE_L1D_NATCONF", "BE_L1D_FPU_BUBBLE_L1D_NATCONF"},  /* { "BE_L1D_FPU_BUBBLE_L1D_NATCONF", {0xf00ca}, 0xf0, 1, {0x5210000}, "Full Pipe Bubbles in Main Pipe due to FPU or L1D Cache -- Back-end was stalled by L1D due to ld8.fill conflict with st8.spill not written to unat."}, */
{35, 35, "PME_ITA2_BE_L1D_FPU_BUBBLE_L1D_STBUFRECIR", "BE_L1D_FPU_BUBBLE_L1D_STBUFRECIR"},  /* { "BE_L1D_FPU_BUBBLE_L1D_STBUFRECIR", {0xe00ca}, 0xf0, 1, {0x5210000}, "Full Pipe Bubbles in Main Pipe due to FPU or L1D Cache -- Back-end was stalled by L1D due to store buffer cancel needing recirculate."}, */
{36, 36, "PME_ITA2_BE_L1D_FPU_BUBBLE_L1D_TLB", "BE_L1D_FPU_BUBBLE_L1D_TLB"},  /* { "BE_L1D_FPU_BUBBLE_L1D_TLB", {0xa00ca}, 0xf0, 1, {0x5210000}, "Full Pipe Bubbles in Main Pipe due to FPU or L1D Cache -- Back-end was stalled by L1D due to L2DTLB to L1DTLB transfer"}, */
{37, 37, "PME_ITA2_BE_LOST_BW_DUE_TO_FE_ALL", "BE_LOST_BW_DUE_TO_FE_ALL"},  /* { "BE_LOST_BW_DUE_TO_FE_ALL", {0x72}, 0xf0, 2, {0xf00000}, "Invalid Bundles if BE Not Stalled for Other Reasons. -- count regardless of cause"}, */
{38, 38, "PME_ITA2_BE_LOST_BW_DUE_TO_FE_BI", "BE_LOST_BW_DUE_TO_FE_BI"},  /* { "BE_LOST_BW_DUE_TO_FE_BI", {0x90072}, 0xf0, 2, {0xf00000}, "Invalid Bundles if BE Not Stalled for Other Reasons. -- only if caused by branch initialization stall"}, */
{39, 39, "PME_ITA2_BE_LOST_BW_DUE_TO_FE_BRQ", "BE_LOST_BW_DUE_TO_FE_BRQ"},  /* { "BE_LOST_BW_DUE_TO_FE_BRQ", {0xa0072}, 0xf0, 2, {0xf00000}, "Invalid Bundles if BE Not Stalled for Other Reasons. -- only if caused by branch retirement queue stall"}, */
{40, 40, "PME_ITA2_BE_LOST_BW_DUE_TO_FE_BR_ILOCK", "BE_LOST_BW_DUE_TO_FE_BR_ILOCK"},  /* { "BE_LOST_BW_DUE_TO_FE_BR_ILOCK", {0xc0072}, 0xf0, 2, {0xf00000}, "Invalid Bundles if BE Not Stalled for Other Reasons. -- only if caused by branch interlock stall"}, */
{41, 41, "PME_ITA2_BE_LOST_BW_DUE_TO_FE_BUBBLE", "BE_LOST_BW_DUE_TO_FE_BUBBLE"},  /* { "BE_LOST_BW_DUE_TO_FE_BUBBLE", {0xd0072}, 0xf0, 2, {0xf00000}, "Invalid Bundles if BE Not Stalled for Other Reasons. -- only if caused by branch resteer bubble stall"}, */
{42, 42, "PME_ITA2_BE_LOST_BW_DUE_TO_FE_FEFLUSH", "BE_LOST_BW_DUE_TO_FE_FEFLUSH"},  /* { "BE_LOST_BW_DUE_TO_FE_FEFLUSH", {0x10072}, 0xf0, 2, {0xf00000}, "Invalid Bundles if BE Not Stalled for Other Reasons. -- only if caused by a front-end flush"}, */
{43, 43, "PME_ITA2_BE_LOST_BW_DUE_TO_FE_FILL_RECIRC", "BE_LOST_BW_DUE_TO_FE_FILL_RECIRC"},  /* { "BE_LOST_BW_DUE_TO_FE_FILL_RECIRC", {0x80072}, 0xf0, 2, {0xf00000}, "Invalid Bundles if BE Not Stalled for Other Reasons. -- only if caused by a recirculate for a cache line fill operation"}, */
{44, 44, "PME_ITA2_BE_LOST_BW_DUE_TO_FE_IBFULL", "BE_LOST_BW_DUE_TO_FE_IBFULL"},  /* { "BE_LOST_BW_DUE_TO_FE_IBFULL", {0x50072}, 0xf0, 2, {0xf00000}, "Invalid Bundles if BE Not Stalled for Other Reasons. -- (* meaningless for this event *)"}, */
{45, 45, "PME_ITA2_BE_LOST_BW_DUE_TO_FE_IMISS", "BE_LOST_BW_DUE_TO_FE_IMISS"},  /* { "BE_LOST_BW_DUE_TO_FE_IMISS", {0x60072}, 0xf0, 2, {0xf00000}, "Invalid Bundles if BE Not Stalled for Other Reasons. -- only if caused by instruction cache miss stall"}, */
{46, 46, "PME_ITA2_BE_LOST_BW_DUE_TO_FE_PLP", "BE_LOST_BW_DUE_TO_FE_PLP"},  /* { "BE_LOST_BW_DUE_TO_FE_PLP", {0xb0072}, 0xf0, 2, {0xf00000}, "Invalid Bundles if BE Not Stalled for Other Reasons. -- only if caused by perfect loop prediction stall"}, */
{47, 47, "PME_ITA2_BE_LOST_BW_DUE_TO_FE_TLBMISS", "BE_LOST_BW_DUE_TO_FE_TLBMISS"},  /* { "BE_LOST_BW_DUE_TO_FE_TLBMISS", {0x70072}, 0xf0, 2, {0xf00000}, "Invalid Bundles if BE Not Stalled for Other Reasons. -- only if caused by TLB stall"}, */
{48, 48, "PME_ITA2_BE_LOST_BW_DUE_TO_FE_UNREACHED", "BE_LOST_BW_DUE_TO_FE_UNREACHED"},  /* { "BE_LOST_BW_DUE_TO_FE_UNREACHED", {0x40072}, 0xf0, 2, {0xf00000}, "Invalid Bundles if BE Not Stalled for Other Reasons. -- only if caused by unreachable bundle"}, */
{49, 49, "PME_ITA2_BE_RSE_BUBBLE_ALL", "BE_RSE_BUBBLE_ALL"},  /* { "BE_RSE_BUBBLE_ALL", {0x1}, 0xf0, 1, {0xf00000}, "Full Pipe Bubbles in Main Pipe due to RSE Stalls -- Back-end was stalled by RSE"}, */
{50, 50, "PME_ITA2_BE_RSE_BUBBLE_AR_DEP", "BE_RSE_BUBBLE_AR_DEP"},  /* { "BE_RSE_BUBBLE_AR_DEP", {0x20001}, 0xf0, 1, {0xf00000}, "Full Pipe Bubbles in Main Pipe due to RSE Stalls -- Back-end was stalled by RSE due to AR dependencies"}, */
{51, 51, "PME_ITA2_BE_RSE_BUBBLE_BANK_SWITCH", "BE_RSE_BUBBLE_BANK_SWITCH"},  /* { "BE_RSE_BUBBLE_BANK_SWITCH", {0x10001}, 0xf0, 1, {0xf00000}, "Full Pipe Bubbles in Main Pipe due to RSE Stalls -- Back-end was stalled by RSE due to bank switching"}, */
{52, 52, "PME_ITA2_BE_RSE_BUBBLE_LOADRS", "BE_RSE_BUBBLE_LOADRS"},  /* { "BE_RSE_BUBBLE_LOADRS", {0x50001}, 0xf0, 1, {0xf00000}, "Full Pipe Bubbles in Main Pipe due to RSE Stalls -- Back-end was stalled by RSE due to loadrs calculations"}, */
{53, 53, "PME_ITA2_BE_RSE_BUBBLE_OVERFLOW", "BE_RSE_BUBBLE_OVERFLOW"},  /* { "BE_RSE_BUBBLE_OVERFLOW", {0x30001}, 0xf0, 1, {0xf00000}, "Full Pipe Bubbles in Main Pipe due to RSE Stalls -- Back-end was stalled by RSE due to need to spill"}, */
{54, 54, "PME_ITA2_BE_RSE_BUBBLE_UNDERFLOW", "BE_RSE_BUBBLE_UNDERFLOW"},  /* { "BE_RSE_BUBBLE_UNDERFLOW", {0x40001}, 0xf0, 1, {0xf00000}, "Full Pipe Bubbles in Main Pipe due to RSE Stalls -- Back-end was stalled by RSE due to need to fill"}, */
{55, 55, "PME_ITA2_BRANCH_EVENT", "BRANCH_EVENT"},  /* { "BRANCH_EVENT", {0x111}, 0xf0, 1, {0xf00003}, "Branch Event Captured"}, */
{56, 56, "PME_ITA2_BR_MISPRED_DETAIL_ALL_ALL_PRED", "BR_MISPRED_DETAIL_ALL_ALL_PRED"},  /* { "BR_MISPRED_DETAIL_ALL_ALL_PRED", {0x5b}, 0xf0, 3, {0xf00003}, "FE Branch Mispredict Detail -- All branch types regardless of prediction result"}, */
{57, 57, "PME_ITA2_BR_MISPRED_DETAIL_ALL_CORRECT_PRED", "BR_MISPRED_DETAIL_ALL_CORRECT_PRED"},  /* { "BR_MISPRED_DETAIL_ALL_CORRECT_PRED", {0x1005b}, 0xf0, 3, {0xf00003}, "FE Branch Mispredict Detail -- All branch types, correctly predicted branches (outcome and target)"}, */
{58, 58, "PME_ITA2_BR_MISPRED_DETAIL_ALL_WRONG_PATH", "BR_MISPRED_DETAIL_ALL_WRONG_PATH"},  /* { "BR_MISPRED_DETAIL_ALL_WRONG_PATH", {0x2005b}, 0xf0, 3, {0xf00003}, "FE Branch Mispredict Detail -- All branch types, mispredicted branches due to wrong branch direction"}, */
{59, 59, "PME_ITA2_BR_MISPRED_DETAIL_ALL_WRONG_TARGET", "BR_MISPRED_DETAIL_ALL_WRONG_TARGET"},  /* { "BR_MISPRED_DETAIL_ALL_WRONG_TARGET", {0x3005b}, 0xf0, 3, {0xf00003}, "FE Branch Mispredict Detail -- All branch types, mispredicted branches due to wrong target for taken branches"}, */
{60, 60, "PME_ITA2_BR_MISPRED_DETAIL_IPREL_ALL_PRED", "BR_MISPRED_DETAIL_IPREL_ALL_PRED"},  /* { "BR_MISPRED_DETAIL_IPREL_ALL_PRED", {0x4005b}, 0xf0, 3, {0xf00003}, "FE Branch Mispredict Detail -- Only IP relative branches, regardless of prediction result"}, */
{61, 61, "PME_ITA2_BR_MISPRED_DETAIL_IPREL_CORRECT_PRED", "BR_MISPRED_DETAIL_IPREL_CORRECT_PRED"},  /* { "BR_MISPRED_DETAIL_IPREL_CORRECT_PRED", {0x5005b}, 0xf0, 3, {0xf00003}, "FE Branch Mispredict Detail -- Only IP relative branches, correctly predicted branches (outcome and target)"}, */
{62, 62, "PME_ITA2_BR_MISPRED_DETAIL_IPREL_WRONG_PATH", "BR_MISPRED_DETAIL_IPREL_WRONG_PATH"},  /* { "BR_MISPRED_DETAIL_IPREL_WRONG_PATH", {0x6005b}, 0xf0, 3, {0xf00003}, "FE Branch Mispredict Detail -- Only IP relative branches, mispredicted branches due to wrong branch direction"}, */
{63, 63, "PME_ITA2_BR_MISPRED_DETAIL_IPREL_WRONG_TARGET", "BR_MISPRED_DETAIL_IPREL_WRONG_TARGET"},  /* { "BR_MISPRED_DETAIL_IPREL_WRONG_TARGET", {0x7005b}, 0xf0, 3, {0xf00003}, "FE Branch Mispredict Detail -- Only IP relative branches, mispredicted branches due to wrong target for taken branches"}, */
{64, 64, "PME_ITA2_BR_MISPRED_DETAIL_NTRETIND_ALL_PRED", "BR_MISPRED_DETAIL_NTRETIND_ALL_PRED"},  /* { "BR_MISPRED_DETAIL_NTRETIND_ALL_PRED", {0xc005b}, 0xf0, 3, {0xf00003}, "FE Branch Mispredict Detail -- Only non-return indirect branches, regardless of prediction result"}, */
{65, 65, "PME_ITA2_BR_MISPRED_DETAIL_NTRETIND_CORRECT_PRED", "BR_MISPRED_DETAIL_NTRETIND_CORRECT_PRED"},  /* { "BR_MISPRED_DETAIL_NTRETIND_CORRECT_PRED", {0xd005b}, 0xf0, 3, {0xf00003}, "FE Branch Mispredict Detail -- Only non-return indirect branches, correctly predicted branches (outcome and target)"}, */
{66, 66, "PME_ITA2_BR_MISPRED_DETAIL_NTRETIND_WRONG_PATH", "BR_MISPRED_DETAIL_NTRETIND_WRONG_PATH"},  /* { "BR_MISPRED_DETAIL_NTRETIND_WRONG_PATH", {0xe005b}, 0xf0, 3, {0xf00003}, "FE Branch Mispredict Detail -- Only non-return indirect branches, mispredicted branches due to wrong branch direction"}, */
{67, 67, "PME_ITA2_BR_MISPRED_DETAIL_NTRETIND_WRONG_TARGET", "BR_MISPRED_DETAIL_NTRETIND_WRONG_TARGET"},  /* { "BR_MISPRED_DETAIL_NTRETIND_WRONG_TARGET", {0xf005b}, 0xf0, 3, {0xf00003}, "FE Branch Mispredict Detail -- Only non-return indirect branches, mispredicted branches due to wrong target for taken branches"}, */
{68, 68, "PME_ITA2_BR_MISPRED_DETAIL_RETURN_ALL_PRED", "BR_MISPRED_DETAIL_RETURN_ALL_PRED"},  /* { "BR_MISPRED_DETAIL_RETURN_ALL_PRED", {0x8005b}, 0xf0, 3, {0xf00003}, "FE Branch Mispredict Detail -- Only return type branches, regardless of prediction result"}, */
{69, 69, "PME_ITA2_BR_MISPRED_DETAIL_RETURN_CORRECT_PRED", "BR_MISPRED_DETAIL_RETURN_CORRECT_PRED"},  /* { "BR_MISPRED_DETAIL_RETURN_CORRECT_PRED", {0x9005b}, 0xf0, 3, {0xf00003}, "FE Branch Mispredict Detail -- Only return type branches, correctly predicted branches (outcome and target)"}, */
{70, 70, "PME_ITA2_BR_MISPRED_DETAIL_RETURN_WRONG_PATH", "BR_MISPRED_DETAIL_RETURN_WRONG_PATH"},  /* { "BR_MISPRED_DETAIL_RETURN_WRONG_PATH", {0xa005b}, 0xf0, 3, {0xf00003}, "FE Branch Mispredict Detail -- Only return type branches, mispredicted branches due to wrong branch direction"}, */
{71, 71, "PME_ITA2_BR_MISPRED_DETAIL_RETURN_WRONG_TARGET", "BR_MISPRED_DETAIL_RETURN_WRONG_TARGET"},  /* { "BR_MISPRED_DETAIL_RETURN_WRONG_TARGET", {0xb005b}, 0xf0, 3, {0xf00003}, "FE Branch Mispredict Detail -- Only return type branches, mispredicted branches due to wrong target for taken branches"}, */
{72, 72, "PME_ITA2_BR_MISPRED_DETAIL2_ALL_ALL_UNKNOWN_PRED", "BR_MISPRED_DETAIL2_ALL_ALL_UNKNOWN_PRED"},  /* { "BR_MISPRED_DETAIL2_ALL_ALL_UNKNOWN_PRED", {0x68}, 0xf0, 2, {0xf00003}, "FE Branch Mispredict Detail (Unknown Path Component) -- All branch types, branches with unknown path prediction"}, */
{73, 73, "PME_ITA2_BR_MISPRED_DETAIL2_ALL_UNKNOWN_PATH_CORRECT_PRED", "BR_MISPRED_DETAIL2_ALL_UNKNOWN_PATH_CORRECT_PRED"},  /* { "BR_MISPRED_DETAIL2_ALL_UNKNOWN_PATH_CORRECT_PRED", {0x10068}, 0xf0, 2, {0xf00003}, "FE Branch Mispredict Detail (Unknown Path Component) -- All branch types, branches with unknown path prediction and correctly predicted branch (outcome & target)"}, */
{74, 74, "PME_ITA2_BR_MISPRED_DETAIL2_ALL_UNKNOWN_PATH_WRONG_PATH", "BR_MISPRED_DETAIL2_ALL_UNKNOWN_PATH_WRONG_PATH"},  /* { "BR_MISPRED_DETAIL2_ALL_UNKNOWN_PATH_WRONG_PATH", {0x20068}, 0xf0, 2, {0xf00003}, "FE Branch Mispredict Detail (Unknown Path Component) -- All branch types, branches with unknown path prediction and wrong branch direction"}, */
{75, 75, "PME_ITA2_BR_MISPRED_DETAIL2_IPREL_ALL_UNKNOWN_PRED", "BR_MISPRED_DETAIL2_IPREL_ALL_UNKNOWN_PRED"},  /* { "BR_MISPRED_DETAIL2_IPREL_ALL_UNKNOWN_PRED", {0x40068}, 0xf0, 2, {0xf00003}, "FE Branch Mispredict Detail (Unknown Path Component) -- Only IP relative branches, branches with unknown path prediction"}, */
{76, 76, "PME_ITA2_BR_MISPRED_DETAIL2_IPREL_UNKNOWN_PATH_CORRECT_PRED", "BR_MISPRED_DETAIL2_IPREL_UNKNOWN_PATH_CORRECT_PRED"},  /* { "BR_MISPRED_DETAIL2_IPREL_UNKNOWN_PATH_CORRECT_PRED", {0x50068}, 0xf0, 2, {0xf00003}, "FE Branch Mispredict Detail (Unknown Path Component) -- Only IP relative branches, branches with unknown path prediction and correct predicted branch (outcome & target)"}, */
{77, 77, "PME_ITA2_BR_MISPRED_DETAIL2_IPREL_UNKNOWN_PATH_WRONG_PATH", "BR_MISPRED_DETAIL2_IPREL_UNKNOWN_PATH_WRONG_PATH"},  /* { "BR_MISPRED_DETAIL2_IPREL_UNKNOWN_PATH_WRONG_PATH", {0x60068}, 0xf0, 2, {0xf00003}, "FE Branch Mispredict Detail (Unknown Path Component) -- Only IP relative branches, branches with unknown path prediction and wrong branch direction"}, */
{78, 78, "PME_ITA2_BR_MISPRED_DETAIL2_NRETIND_ALL_UNKNOWN_PRED", "BR_MISPRED_DETAIL2_NRETIND_ALL_UNKNOWN_PRED"},  /* { "BR_MISPRED_DETAIL2_NRETIND_ALL_UNKNOWN_PRED", {0xc0068}, 0xf0, 2, {0xf00003}, "FE Branch Mispredict Detail (Unknown Path Component) -- Only non-return indirect branches, branches with unknown path prediction"}, */
{79, 79, "PME_ITA2_BR_MISPRED_DETAIL2_NRETIND_UNKNOWN_PATH_CORRECT_PRED", "BR_MISPRED_DETAIL2_NRETIND_UNKNOWN_PATH_CORRECT_PRED"},  /* { "BR_MISPRED_DETAIL2_NRETIND_UNKNOWN_PATH_CORRECT_PRED", {0xd0068}, 0xf0, 2, {0xf00003}, "FE Branch Mispredict Detail (Unknown Path Component) -- Only non-return indirect branches, branches with unknown path prediction and correct predicted branch (outcome & target)"}, */
{80, 80, "PME_ITA2_BR_MISPRED_DETAIL2_NRETIND_UNKNOWN_PATH_WRONG_PATH", "BR_MISPRED_DETAIL2_NRETIND_UNKNOWN_PATH_WRONG_PATH"},  /* { "BR_MISPRED_DETAIL2_NRETIND_UNKNOWN_PATH_WRONG_PATH", {0xe0068}, 0xf0, 2, {0xf00003}, "FE Branch Mispredict Detail (Unknown Path Component) -- Only non-return indirect branches, branches with unknown path prediction and wrong branch direction"}, */
{81, 81, "PME_ITA2_BR_MISPRED_DETAIL2_RETURN_ALL_UNKNOWN_PRED", "BR_MISPRED_DETAIL2_RETURN_ALL_UNKNOWN_PRED"},  /* { "BR_MISPRED_DETAIL2_RETURN_ALL_UNKNOWN_PRED", {0x80068}, 0xf0, 2, {0xf00003}, "FE Branch Mispredict Detail (Unknown Path Component) -- Only return type branches, branches with unknown path prediction"}, */
{82, 82, "PME_ITA2_BR_MISPRED_DETAIL2_RETURN_UNKNOWN_PATH_CORRECT_PRED", "BR_MISPRED_DETAIL2_RETURN_UNKNOWN_PATH_CORRECT_PRED"},  /* { "BR_MISPRED_DETAIL2_RETURN_UNKNOWN_PATH_CORRECT_PRED", {0x90068}, 0xf0, 2, {0xf00003}, "FE Branch Mispredict Detail (Unknown Path Component) -- Only return type branches, branches with unknown path prediction and correct predicted branch (outcome & target)"}, */
{83, 83, "PME_ITA2_BR_MISPRED_DETAIL2_RETURN_UNKNOWN_PATH_WRONG_PATH", "BR_MISPRED_DETAIL2_RETURN_UNKNOWN_PATH_WRONG_PATH"},  /* { "BR_MISPRED_DETAIL2_RETURN_UNKNOWN_PATH_WRONG_PATH", {0xa0068}, 0xf0, 2, {0xf00003}, "FE Branch Mispredict Detail (Unknown Path Component) -- Only return type branches, branches with unknown path prediction and wrong branch direction"}, */
{84, 84, "PME_ITA2_BR_PATH_PRED_ALL_MISPRED_NOTTAKEN", "BR_PATH_PRED_ALL_MISPRED_NOTTAKEN"},  /* { "BR_PATH_PRED_ALL_MISPRED_NOTTAKEN", {0x54}, 0xf0, 3, {0xf00003}, "FE Branch Path Prediction Detail -- All branch types, incorrectly predicted path and not taken branch"}, */
{85, 85, "PME_ITA2_BR_PATH_PRED_ALL_MISPRED_TAKEN", "BR_PATH_PRED_ALL_MISPRED_TAKEN"},  /* { "BR_PATH_PRED_ALL_MISPRED_TAKEN", {0x10054}, 0xf0, 3, {0xf00003}, "FE Branch Path Prediction Detail -- All branch types, incorrectly predicted path and taken branch"}, */
{86, 86, "PME_ITA2_BR_PATH_PRED_ALL_OKPRED_NOTTAKEN", "BR_PATH_PRED_ALL_OKPRED_NOTTAKEN"},  /* { "BR_PATH_PRED_ALL_OKPRED_NOTTAKEN", {0x20054}, 0xf0, 3, {0xf00003}, "FE Branch Path Prediction Detail -- All branch types, correctly predicted path and not taken branch"}, */
{87, 87, "PME_ITA2_BR_PATH_PRED_ALL_OKPRED_TAKEN", "BR_PATH_PRED_ALL_OKPRED_TAKEN"},  /* { "BR_PATH_PRED_ALL_OKPRED_TAKEN", {0x30054}, 0xf0, 3, {0xf00003}, "FE Branch Path Prediction Detail -- All branch types, correctly predicted path and taken branch"}, */
{88, 88, "PME_ITA2_BR_PATH_PRED_IPREL_MISPRED_NOTTAKEN", "BR_PATH_PRED_IPREL_MISPRED_NOTTAKEN"},  /* { "BR_PATH_PRED_IPREL_MISPRED_NOTTAKEN", {0x40054}, 0xf0, 3, {0xf00003}, "FE Branch Path Prediction Detail -- Only IP relative branches, incorrectly predicted path and not taken branch"}, */
{89, 89, "PME_ITA2_BR_PATH_PRED_IPREL_MISPRED_TAKEN", "BR_PATH_PRED_IPREL_MISPRED_TAKEN"},  /* { "BR_PATH_PRED_IPREL_MISPRED_TAKEN", {0x50054}, 0xf0, 3, {0xf00003}, "FE Branch Path Prediction Detail -- Only IP relative branches, incorrectly predicted path and taken branch"}, */
{90, 90, "PME_ITA2_BR_PATH_PRED_IPREL_OKPRED_NOTTAKEN", "BR_PATH_PRED_IPREL_OKPRED_NOTTAKEN"},  /* { "BR_PATH_PRED_IPREL_OKPRED_NOTTAKEN", {0x60054}, 0xf0, 3, {0xf00003}, "FE Branch Path Prediction Detail -- Only IP relative branches, correctly predicted path and not taken branch"}, */
{91, 91, "PME_ITA2_BR_PATH_PRED_IPREL_OKPRED_TAKEN", "BR_PATH_PRED_IPREL_OKPRED_TAKEN"},  /* { "BR_PATH_PRED_IPREL_OKPRED_TAKEN", {0x70054}, 0xf0, 3, {0xf00003}, "FE Branch Path Prediction Detail -- Only IP relative branches, correctly predicted path and taken branch"}, */
{92, 92, "PME_ITA2_BR_PATH_PRED_NRETIND_MISPRED_NOTTAKEN", "BR_PATH_PRED_NRETIND_MISPRED_NOTTAKEN"},  /* { "BR_PATH_PRED_NRETIND_MISPRED_NOTTAKEN", {0xc0054}, 0xf0, 3, {0xf00003}, "FE Branch Path Prediction Detail -- Only non-return indirect branches, incorrectly predicted path and not taken branch"}, */
{93, 93, "PME_ITA2_BR_PATH_PRED_NRETIND_MISPRED_TAKEN", "BR_PATH_PRED_NRETIND_MISPRED_TAKEN"},  /* { "BR_PATH_PRED_NRETIND_MISPRED_TAKEN", {0xd0054}, 0xf0, 3, {0xf00003}, "FE Branch Path Prediction Detail -- Only non-return indirect branches, incorrectly predicted path and taken branch"}, */
{94, 94, "PME_ITA2_BR_PATH_PRED_NRETIND_OKPRED_NOTTAKEN", "BR_PATH_PRED_NRETIND_OKPRED_NOTTAKEN"},  /* { "BR_PATH_PRED_NRETIND_OKPRED_NOTTAKEN", {0xe0054}, 0xf0, 3, {0xf00003}, "FE Branch Path Prediction Detail -- Only non-return indirect branches, correctly predicted path and not taken branch"}, */
{95, 95, "PME_ITA2_BR_PATH_PRED_NRETIND_OKPRED_TAKEN", "BR_PATH_PRED_NRETIND_OKPRED_TAKEN"},  /* { "BR_PATH_PRED_NRETIND_OKPRED_TAKEN", {0xf0054}, 0xf0, 3, {0xf00003}, "FE Branch Path Prediction Detail -- Only non-return indirect branches, correctly predicted path and taken branch"}, */
{96, 96, "PME_ITA2_BR_PATH_PRED_RETURN_MISPRED_NOTTAKEN", "BR_PATH_PRED_RETURN_MISPRED_NOTTAKEN"},  /* { "BR_PATH_PRED_RETURN_MISPRED_NOTTAKEN", {0x80054}, 0xf0, 3, {0xf00003}, "FE Branch Path Prediction Detail -- Only return type branches, incorrectly predicted path and not taken branch"}, */
{97, 97, "PME_ITA2_BR_PATH_PRED_RETURN_MISPRED_TAKEN", "BR_PATH_PRED_RETURN_MISPRED_TAKEN"},  /* { "BR_PATH_PRED_RETURN_MISPRED_TAKEN", {0x90054}, 0xf0, 3, {0xf00003}, "FE Branch Path Prediction Detail -- Only return type branches, incorrectly predicted path and taken branch"}, */
{98, 98, "PME_ITA2_BR_PATH_PRED_RETURN_OKPRED_NOTTAKEN", "BR_PATH_PRED_RETURN_OKPRED_NOTTAKEN"},  /* { "BR_PATH_PRED_RETURN_OKPRED_NOTTAKEN", {0xa0054}, 0xf0, 3, {0xf00003}, "FE Branch Path Prediction Detail -- Only return type branches, correctly predicted path and not taken branch"}, */
{99, 99, "PME_ITA2_BR_PATH_PRED_RETURN_OKPRED_TAKEN", "BR_PATH_PRED_RETURN_OKPRED_TAKEN"},  /* { "BR_PATH_PRED_RETURN_OKPRED_TAKEN", {0xb0054}, 0xf0, 3, {0xf00003}, "FE Branch Path Prediction Detail -- Only return type branches, correctly predicted path and taken branch"}, */
{100, 100, "PME_ITA2_BR_PATH_PRED2_ALL_UNKNOWNPRED_NOTTAKEN", "BR_PATH_PRED2_ALL_UNKNOWNPRED_NOTTAKEN"},  /* { "BR_PATH_PRED2_ALL_UNKNOWNPRED_NOTTAKEN", {0x6a}, 0xf0, 2, {0xf00003}, "FE Branch Path Prediction Detail (Unknown pred component) -- All branch types, unknown predicted path and not taken branch (which impacts OKPRED_NOTTAKEN)"}, */
{101, 101, "PME_ITA2_BR_PATH_PRED2_ALL_UNKNOWNPRED_TAKEN", "BR_PATH_PRED2_ALL_UNKNOWNPRED_TAKEN"},  /* { "BR_PATH_PRED2_ALL_UNKNOWNPRED_TAKEN", {0x1006a}, 0xf0, 2, {0xf00003}, "FE Branch Path Prediction Detail (Unknown pred component) -- All branch types, unknown predicted path and taken branch (which impacts MISPRED_TAKEN)"}, */
{102, 102, "PME_ITA2_BR_PATH_PRED2_IPREL_UNKNOWNPRED_NOTTAKEN", "BR_PATH_PRED2_IPREL_UNKNOWNPRED_NOTTAKEN"},  /* { "BR_PATH_PRED2_IPREL_UNKNOWNPRED_NOTTAKEN", {0x4006a}, 0xf0, 2, {0xf00003}, "FE Branch Path Prediction Detail (Unknown pred component) -- Only IP relative branches, unknown predicted path and not taken branch (which impacts OKPRED_NOTTAKEN)"}, */
{103, 103, "PME_ITA2_BR_PATH_PRED2_IPREL_UNKNOWNPRED_TAKEN", "BR_PATH_PRED2_IPREL_UNKNOWNPRED_TAKEN"},  /* { "BR_PATH_PRED2_IPREL_UNKNOWNPRED_TAKEN", {0x5006a}, 0xf0, 2, {0xf00003}, "FE Branch Path Prediction Detail (Unknown pred component) -- Only IP relative branches, unknown predicted path and taken branch (which impacts MISPRED_TAKEN)"}, */
{104, 104, "PME_ITA2_BR_PATH_PRED2_NRETIND_UNKNOWNPRED_NOTTAKEN", "BR_PATH_PRED2_NRETIND_UNKNOWNPRED_NOTTAKEN"},  /* { "BR_PATH_PRED2_NRETIND_UNKNOWNPRED_NOTTAKEN", {0xc006a}, 0xf0, 2, {0xf00003}, "FE Branch Path Prediction Detail (Unknown pred component) -- Only non-return indirect branches, unknown predicted path and not taken branch (which impacts OKPRED_NOTTAKEN)"}, */
{105, 105, "PME_ITA2_BR_PATH_PRED2_NRETIND_UNKNOWNPRED_TAKEN", "BR_PATH_PRED2_NRETIND_UNKNOWNPRED_TAKEN"},  /* { "BR_PATH_PRED2_NRETIND_UNKNOWNPRED_TAKEN", {0xd006a}, 0xf0, 2, {0xf00003}, "FE Branch Path Prediction Detail (Unknown pred component) -- Only non-return indirect branches, unknown predicted path and taken branch (which impacts MISPRED_TAKEN)"}, */
{106, 106, "PME_ITA2_BR_PATH_PRED2_RETURN_UNKNOWNPRED_NOTTAKEN", "BR_PATH_PRED2_RETURN_UNKNOWNPRED_NOTTAKEN"},  /* { "BR_PATH_PRED2_RETURN_UNKNOWNPRED_NOTTAKEN", {0x8006a}, 0xf0, 2, {0xf00003}, "FE Branch Path Prediction Detail (Unknown pred component) -- Only return type branches, unknown predicted path and not taken branch (which impacts OKPRED_NOTTAKEN)"}, */
{107, 107, "PME_ITA2_BR_PATH_PRED2_RETURN_UNKNOWNPRED_TAKEN", "BR_PATH_PRED2_RETURN_UNKNOWNPRED_TAKEN"},  /* { "BR_PATH_PRED2_RETURN_UNKNOWNPRED_TAKEN", {0x9006a}, 0xf0, 2, {0xf00003}, "FE Branch Path Prediction Detail (Unknown pred component) -- Only return type branches, unknown predicted path and taken branch (which impacts MISPRED_TAKEN)"}, */
{108, 108, "PME_ITA2_BUS_ALL_ANY", "BUS_ALL_ANY"},  /* { "BUS_ALL_ANY", {0x30087}, 0xf0, 1, {0xf00000}, "Bus Transactions -- CPU or non-CPU (all transactions)."}, */
{109, 109, "PME_ITA2_BUS_ALL_IO", "BUS_ALL_IO"},  /* { "BUS_ALL_IO", {0x10087}, 0xf0, 1, {0xf00000}, "Bus Transactions -- non-CPU priority agents"}, */
{110, 110, "PME_ITA2_BUS_ALL_SELF", "BUS_ALL_SELF"},  /* { "BUS_ALL_SELF", {0x20087}, 0xf0, 1, {0xf00000}, "Bus Transactions -- local processor"}, */
{111, 111, "PME_ITA2_BUS_BACKSNP_REQ_THIS", "BUS_BACKSNP_REQ_THIS"},  /* { "BUS_BACKSNP_REQ_THIS", {0x1008e}, 0xf0, 1, {0xf00000}, "Bus Back Snoop Requests -- Counts the number of bus back snoop me requests"}, */
{112, 112, "PME_ITA2_BUS_BRQ_LIVE_REQ_HI", "BUS_BRQ_LIVE_REQ_HI"},  /* { "BUS_BRQ_LIVE_REQ_HI", {0x9c}, 0xf0, 2, {0xf00000}, "BRQ Live Requests (upper 2 bits)"}, */
{113, 113, "PME_ITA2_BUS_BRQ_LIVE_REQ_LO", "BUS_BRQ_LIVE_REQ_LO"},  /* { "BUS_BRQ_LIVE_REQ_LO", {0x9b}, 0xf0, 7, {0xf00000}, "BRQ Live Requests (lower 3 bits)"}, */
{114, 114, "PME_ITA2_BUS_BRQ_REQ_INSERTED", "BUS_BRQ_REQ_INSERTED"},  /* { "BUS_BRQ_REQ_INSERTED", {0x9d}, 0xf0, 1, {0xf00000}, "BRQ Requests Inserted"}, */
{115, 115, "PME_ITA2_BUS_DATA_CYCLE", "BUS_DATA_CYCLE"},  /* { "BUS_DATA_CYCLE", {0x88}, 0xf0, 1, {0xf00000}, "Valid Data Cycle on the Bus"}, */
{116, 116, "PME_ITA2_BUS_HITM", "BUS_HITM"},  /* { "BUS_HITM", {0x84}, 0xf0, 1, {0xf00000}, "Bus Hit Modified Line Transactions"}, */
{117, 117, "PME_ITA2_BUS_IO_ANY", "BUS_IO_ANY"},  /* { "BUS_IO_ANY", {0x30090}, 0xf0, 1, {0xf00000}, "IA-32 Compatible IO Bus Transactions -- CPU or non-CPU (all transactions)."}, */
{118, 118, "PME_ITA2_BUS_IO_IO", "BUS_IO_IO"},  /* { "BUS_IO_IO", {0x10090}, 0xf0, 1, {0xf00000}, "IA-32 Compatible IO Bus Transactions -- non-CPU priority agents"}, */
{119, 119, "PME_ITA2_BUS_IO_SELF", "BUS_IO_SELF"},  /* { "BUS_IO_SELF", {0x20090}, 0xf0, 1, {0xf00000}, "IA-32 Compatible IO Bus Transactions -- local processor"}, */
{120, 120, "PME_ITA2_BUS_IOQ_LIVE_REQ_HI", "BUS_IOQ_LIVE_REQ_HI"},  /* { "BUS_IOQ_LIVE_REQ_HI", {0x98}, 0xf0, 2, {0xf00000}, "Inorder Bus Queue Requests (upper 2 bits)"}, */
{121, 121, "PME_ITA2_BUS_IOQ_LIVE_REQ_LO", "BUS_IOQ_LIVE_REQ_LO"},  /* { "BUS_IOQ_LIVE_REQ_LO", {0x97}, 0xf0, 3, {0xf00000}, "Inorder Bus Queue Requests (lower2 bitst)"}, */
{122, 122, "PME_ITA2_BUS_LOCK_ANY", "BUS_LOCK_ANY"},  /* { "BUS_LOCK_ANY", {0x30093}, 0xf0, 1, {0xf00000}, "IA-32 Compatible Bus Lock Transactions -- CPU or non-CPU (all transactions)."}, */
{123, 123, "PME_ITA2_BUS_LOCK_SELF", "BUS_LOCK_SELF"},  /* { "BUS_LOCK_SELF", {0x20093}, 0xf0, 1, {0xf00000}, "IA-32 Compatible Bus Lock Transactions -- local processor"}, */
{124, 124, "PME_ITA2_BUS_MEMORY_ALL_ANY", "BUS_MEMORY_ALL_ANY"},  /* { "BUS_MEMORY_ALL_ANY", {0xf008a}, 0xf0, 1, {0xf00000}, "Bus Memory Transactions -- All bus transactions from CPU or non-CPU (all transactions)."}, */
{125, 125, "PME_ITA2_BUS_MEMORY_ALL_IO", "BUS_MEMORY_ALL_IO"},  /* { "BUS_MEMORY_ALL_IO", {0xd008a}, 0xf0, 1, {0xf00000}, "Bus Memory Transactions -- All bus transactions from non-CPU priority agents"}, */
{126, 126, "PME_ITA2_BUS_MEMORY_ALL_SELF", "BUS_MEMORY_ALL_SELF"},  /* { "BUS_MEMORY_ALL_SELF", {0xe008a}, 0xf0, 1, {0xf00000}, "Bus Memory Transactions -- All bus transactions from local processor"}, */
{127, 127, "PME_ITA2_BUS_MEMORY_EQ_128BYTE_ANY", "BUS_MEMORY_EQ_128BYTE_ANY"},  /* { "BUS_MEMORY_EQ_128BYTE_ANY", {0x7008a}, 0xf0, 1, {0xf00000}, "Bus Memory Transactions -- number of full cache line transactions (BRL, BRIL, BWL) from CPU or non-CPU (all transactions)."}, */
{128, 128, "PME_ITA2_BUS_MEMORY_EQ_128BYTE_IO", "BUS_MEMORY_EQ_128BYTE_IO"},  /* { "BUS_MEMORY_EQ_128BYTE_IO", {0x5008a}, 0xf0, 1, {0xf00000}, "Bus Memory Transactions -- number of full cache line transactions (BRL, BRIL, BWL) from non-CPU priority agents"}, */
{129, 129, "PME_ITA2_BUS_MEMORY_EQ_128BYTE_SELF", "BUS_MEMORY_EQ_128BYTE_SELF"},  /* { "BUS_MEMORY_EQ_128BYTE_SELF", {0x6008a}, 0xf0, 1, {0xf00000}, "Bus Memory Transactions -- number of full cache line transactions (BRL, BRIL, BWL) from local processor"}, */
{130, 130, "PME_ITA2_BUS_MEMORY_LT_128BYTE_ANY", "BUS_MEMORY_LT_128BYTE_ANY"},  /* { "BUS_MEMORY_LT_128BYTE_ANY", {0xb008a}, 0xf0, 1, {0xf00000}, "Bus Memory Transactions -- number of less than full cache line transactions (BRP, BWP) CPU or non-CPU (all transactions)."}, */
{131, 131, "PME_ITA2_BUS_MEMORY_LT_128BYTE_IO", "BUS_MEMORY_LT_128BYTE_IO"},  /* { "BUS_MEMORY_LT_128BYTE_IO", {0x9008a}, 0xf0, 1, {0xf00000}, "Bus Memory Transactions -- number of less than full cache line transactions (BRP, BWP) from non-CPU priority agents"}, */
{132, 132, "PME_ITA2_BUS_MEMORY_LT_128BYTE_SELF", "BUS_MEMORY_LT_128BYTE_SELF"},  /* { "BUS_MEMORY_LT_128BYTE_SELF", {0xa008a}, 0xf0, 1, {0xf00000}, "Bus Memory Transactions -- number of less than full cache line transactions (BRP, BWP) local processor"}, */
{133, 133, "PME_ITA2_BUS_MEM_READ_ALL_ANY", "BUS_MEM_READ_ALL_ANY"},  /* { "BUS_MEM_READ_ALL_ANY", {0xf008b}, 0xf0, 1, {0xf00000}, "Full Cache Line D/I Memory RD, RD Invalidate, and BRIL -- All memory read transactions from CPU or non-CPU (all transactions)."}, */
{134, 134, "PME_ITA2_BUS_MEM_READ_ALL_IO", "BUS_MEM_READ_ALL_IO"},  /* { "BUS_MEM_READ_ALL_IO", {0xd008b}, 0xf0, 1, {0xf00000}, "Full Cache Line D/I Memory RD, RD Invalidate, and BRIL -- All memory read transactions from non-CPU priority agents"}, */
{135, 135, "PME_ITA2_BUS_MEM_READ_ALL_SELF", "BUS_MEM_READ_ALL_SELF"},  /* { "BUS_MEM_READ_ALL_SELF", {0xe008b}, 0xf0, 1, {0xf00000}, "Full Cache Line D/I Memory RD, RD Invalidate, and BRIL -- All memory read transactions from local processor"}, */
{136, 136, "PME_ITA2_BUS_MEM_READ_BIL_ANY", "BUS_MEM_READ_BIL_ANY"},  /* { "BUS_MEM_READ_BIL_ANY", {0x3008b}, 0xf0, 1, {0xf00000}, "Full Cache Line D/I Memory RD, RD Invalidate, and BRIL -- Number of BIL 0-byte memory read invalidate transactions from CPU or non-CPU (all transactions)."}, */
{137, 137, "PME_ITA2_BUS_MEM_READ_BIL_IO", "BUS_MEM_READ_BIL_IO"},  /* { "BUS_MEM_READ_BIL_IO", {0x1008b}, 0xf0, 1, {0xf00000}, "Full Cache Line D/I Memory RD, RD Invalidate, and BRIL -- Number of BIL 0-byte memory read invalidate transactions from non-CPU priority agents"}, */
{138, 138, "PME_ITA2_BUS_MEM_READ_BIL_SELF", "BUS_MEM_READ_BIL_SELF"},  /* { "BUS_MEM_READ_BIL_SELF", {0x2008b}, 0xf0, 1, {0xf00000}, "Full Cache Line D/I Memory RD, RD Invalidate, and BRIL -- Number of BIL 0-byte memory read invalidate transactions from local processor"}, */
{139, 139, "PME_ITA2_BUS_MEM_READ_BRIL_ANY", "BUS_MEM_READ_BRIL_ANY"},  /* { "BUS_MEM_READ_BRIL_ANY", {0xb008b}, 0xf0, 1, {0xf00000}, "Full Cache Line D/I Memory RD, RD Invalidate, and BRIL -- Number of full cache line memory read invalidate transactions from CPU or non-CPU (all transactions)."}, */
{140, 140, "PME_ITA2_BUS_MEM_READ_BRIL_IO", "BUS_MEM_READ_BRIL_IO"},  /* { "BUS_MEM_READ_BRIL_IO", {0x9008b}, 0xf0, 1, {0xf00000}, "Full Cache Line D/I Memory RD, RD Invalidate, and BRIL -- Number of full cache line memory read invalidate transactions from non-CPU priority agents"}, */
{141, 141, "PME_ITA2_BUS_MEM_READ_BRIL_SELF", "BUS_MEM_READ_BRIL_SELF"},  /* { "BUS_MEM_READ_BRIL_SELF", {0xa008b}, 0xf0, 1, {0xf00000}, "Full Cache Line D/I Memory RD, RD Invalidate, and BRIL -- Number of full cache line memory read invalidate transactions from local processor"}, */
{142, 142, "PME_ITA2_BUS_MEM_READ_BRL_ANY", "BUS_MEM_READ_BRL_ANY"},  /* { "BUS_MEM_READ_BRL_ANY", {0x7008b}, 0xf0, 1, {0xf00000}, "Full Cache Line D/I Memory RD, RD Invalidate, and BRIL -- Number of full cache line memory read transactions from CPU or non-CPU (all transactions)."}, */
{143, 143, "PME_ITA2_BUS_MEM_READ_BRL_IO", "BUS_MEM_READ_BRL_IO"},  /* { "BUS_MEM_READ_BRL_IO", {0x5008b}, 0xf0, 1, {0xf00000}, "Full Cache Line D/I Memory RD, RD Invalidate, and BRIL -- Number of full cache line memory read transactions from non-CPU priority agents"}, */
{144, 144, "PME_ITA2_BUS_MEM_READ_BRL_SELF", "BUS_MEM_READ_BRL_SELF"},  /* { "BUS_MEM_READ_BRL_SELF", {0x6008b}, 0xf0, 1, {0xf00000}, "Full Cache Line D/I Memory RD, RD Invalidate, and BRIL -- Number of full cache line memory read transactions from local processor"}, */
{145, 145, "PME_ITA2_BUS_MEM_READ_OUT_HI", "BUS_MEM_READ_OUT_HI"},  /* { "BUS_MEM_READ_OUT_HI", {0x94}, 0xf0, 2, {0xf00000}, "Outstanding Memory Read Transactions (upper 2 bits)"}, */
{146, 146, "PME_ITA2_BUS_MEM_READ_OUT_LO", "BUS_MEM_READ_OUT_LO"},  /* { "BUS_MEM_READ_OUT_LO", {0x95}, 0xf0, 7, {0xf00000}, "Outstanding Memory Read Transactions (lower 3 bits)"}, */
{147, 147, "PME_ITA2_BUS_OOQ_LIVE_REQ_HI", "BUS_OOQ_LIVE_REQ_HI"},  /* { "BUS_OOQ_LIVE_REQ_HI", {0x9a}, 0xf0, 2, {0xf00000}, "Out-of-order Bus Queue Requests (upper 2 bits)"}, */
{148, 148, "PME_ITA2_BUS_OOQ_LIVE_REQ_LO", "BUS_OOQ_LIVE_REQ_LO"},  /* { "BUS_OOQ_LIVE_REQ_LO", {0x99}, 0xf0, 7, {0xf00000}, "Out-of-order Bus Queue Requests (lower 3 bits)"}, */
{149, 149, "PME_ITA2_BUS_RD_DATA_ANY", "BUS_RD_DATA_ANY"},  /* { "BUS_RD_DATA_ANY", {0x3008c}, 0xf0, 1, {0xf00000}, "Bus Read Data Transactions -- CPU or non-CPU (all transactions)."}, */
{150, 150, "PME_ITA2_BUS_RD_DATA_IO", "BUS_RD_DATA_IO"},  /* { "BUS_RD_DATA_IO", {0x1008c}, 0xf0, 1, {0xf00000}, "Bus Read Data Transactions -- non-CPU priority agents"}, */
{151, 151, "PME_ITA2_BUS_RD_DATA_SELF", "BUS_RD_DATA_SELF"},  /* { "BUS_RD_DATA_SELF", {0x2008c}, 0xf0, 1, {0xf00000}, "Bus Read Data Transactions -- local processor"}, */
{152, 152, "PME_ITA2_BUS_RD_HIT", "BUS_RD_HIT"},  /* { "BUS_RD_HIT", {0x80}, 0xf0, 1, {0xf00000}, "Bus Read Hit Clean Non-local Cache Transactions"}, */
{153, 153, "PME_ITA2_BUS_RD_HITM", "BUS_RD_HITM"},  /* { "BUS_RD_HITM", {0x81}, 0xf0, 1, {0xf00000}, "Bus Read Hit Modified Non-local Cache Transactions"}, */
{154, 154, "PME_ITA2_BUS_RD_INVAL_ALL_HITM", "BUS_RD_INVAL_ALL_HITM"},  /* { "BUS_RD_INVAL_ALL_HITM", {0x83}, 0xf0, 1, {0xf00000}, "Bus BRIL Burst Transaction Results in HITM"}, */
{155, 155, "PME_ITA2_BUS_RD_INVAL_HITM", "BUS_RD_INVAL_HITM"},  /* { "BUS_RD_INVAL_HITM", {0x82}, 0xf0, 1, {0xf00000}, "Bus BIL Transaction Results in HITM"}, */
{156, 156, "PME_ITA2_BUS_RD_IO_ANY", "BUS_RD_IO_ANY"},  /* { "BUS_RD_IO_ANY", {0x30091}, 0xf0, 1, {0xf00000}, "IA-32 Compatible IO Read Transactions -- CPU or non-CPU (all transactions)."}, */
{157, 157, "PME_ITA2_BUS_RD_IO_IO", "BUS_RD_IO_IO"},  /* { "BUS_RD_IO_IO", {0x10091}, 0xf0, 1, {0xf00000}, "IA-32 Compatible IO Read Transactions -- non-CPU priority agents"}, */
{158, 158, "PME_ITA2_BUS_RD_IO_SELF", "BUS_RD_IO_SELF"},  /* { "BUS_RD_IO_SELF", {0x20091}, 0xf0, 1, {0xf00000}, "IA-32 Compatible IO Read Transactions -- local processor"}, */
{159, 159, "PME_ITA2_BUS_RD_PRTL_ANY", "BUS_RD_PRTL_ANY"},  /* { "BUS_RD_PRTL_ANY", {0x3008d}, 0xf0, 1, {0xf00000}, "Bus Read Partial Transactions -- CPU or non-CPU (all transactions)."}, */
{160, 160, "PME_ITA2_BUS_RD_PRTL_IO", "BUS_RD_PRTL_IO"},  /* { "BUS_RD_PRTL_IO", {0x1008d}, 0xf0, 1, {0xf00000}, "Bus Read Partial Transactions -- non-CPU priority agents"}, */
{161, 161, "PME_ITA2_BUS_RD_PRTL_SELF", "BUS_RD_PRTL_SELF"},  /* { "BUS_RD_PRTL_SELF", {0x2008d}, 0xf0, 1, {0xf00000}, "Bus Read Partial Transactions -- local processor"}, */
{162, 162, "PME_ITA2_BUS_SNOOPQ_REQ", "BUS_SNOOPQ_REQ"},  /* { "BUS_SNOOPQ_REQ", {0x96}, 0xf0, 7, {0xf00000}, "Bus Snoop Queue Requests"}, */
{163, 163, "PME_ITA2_BUS_SNOOPS_ANY", "BUS_SNOOPS_ANY"},  /* { "BUS_SNOOPS_ANY", {0x30086}, 0xf0, 1, {0xf00000}, "Bus Snoops Total -- CPU or non-CPU (all transactions)."}, */
{164, 164, "PME_ITA2_BUS_SNOOPS_IO", "BUS_SNOOPS_IO"},  /* { "BUS_SNOOPS_IO", {0x10086}, 0xf0, 1, {0xf00000}, "Bus Snoops Total -- non-CPU priority agents"}, */
{165, 165, "PME_ITA2_BUS_SNOOPS_SELF", "BUS_SNOOPS_SELF"},  /* { "BUS_SNOOPS_SELF", {0x20086}, 0xf0, 1, {0xf00000}, "Bus Snoops Total -- local processor"}, */
{166, 166, "PME_ITA2_BUS_SNOOPS_HITM_ANY", "BUS_SNOOPS_HITM_ANY"},  /* { "BUS_SNOOPS_HITM_ANY", {0x30085}, 0xf0, 1, {0xf00000}, "Bus Snoops HIT Modified Cache Line -- CPU or non-CPU (all transactions)."}, */
{167, 167, "PME_ITA2_BUS_SNOOPS_HITM_SELF", "BUS_SNOOPS_HITM_SELF"},  /* { "BUS_SNOOPS_HITM_SELF", {0x20085}, 0xf0, 1, {0xf00000}, "Bus Snoops HIT Modified Cache Line -- local processor"}, */
{168, 168, "PME_ITA2_BUS_SNOOP_STALL_CYCLES_ANY", "BUS_SNOOP_STALL_CYCLES_ANY"},  /* { "BUS_SNOOP_STALL_CYCLES_ANY", {0x3008f}, 0xf0, 1, {0xf00000}, "Bus Snoop Stall Cycles (from any agent) -- CPU or non-CPU (all transactions)."}, */
{169, 169, "PME_ITA2_BUS_SNOOP_STALL_CYCLES_SELF", "BUS_SNOOP_STALL_CYCLES_SELF"},  /* { "BUS_SNOOP_STALL_CYCLES_SELF", {0x2008f}, 0xf0, 1, {0xf00000}, "Bus Snoop Stall Cycles (from any agent) -- local processor"}, */
{170, 170, "PME_ITA2_BUS_WR_WB_ALL_ANY", "BUS_WR_WB_ALL_ANY"},  /* { "BUS_WR_WB_ALL_ANY", {0xf0092}, 0xf0, 1, {0xf00000}, "Bus Write Back Transactions -- CPU or non-CPU (all transactions)."}, */
{171, 171, "PME_ITA2_BUS_WR_WB_ALL_IO", "BUS_WR_WB_ALL_IO"},  /* { "BUS_WR_WB_ALL_IO", {0xd0092}, 0xf0, 1, {0xf00000}, "Bus Write Back Transactions -- non-CPU priority agents"}, */
{172, 172, "PME_ITA2_BUS_WR_WB_ALL_SELF", "BUS_WR_WB_ALL_SELF"},  /* { "BUS_WR_WB_ALL_SELF", {0xe0092}, 0xf0, 1, {0xf00000}, "Bus Write Back Transactions -- local processor"}, */
{173, 173, "PME_ITA2_BUS_WR_WB_CCASTOUT_ANY", "BUS_WR_WB_CCASTOUT_ANY"},  /* { "BUS_WR_WB_CCASTOUT_ANY", {0xb0092}, 0xf0, 1, {0xf00000}, "Bus Write Back Transactions -- CPU or non-CPU (all transactions)/Only 0-byte transactions with write back attribute (clean cast outs) will be counted"}, */
{174, 174, "PME_ITA2_BUS_WR_WB_CCASTOUT_SELF", "BUS_WR_WB_CCASTOUT_SELF"},  /* { "BUS_WR_WB_CCASTOUT_SELF", {0xa0092}, 0xf0, 1, {0xf00000}, "Bus Write Back Transactions -- local processor/Only 0-byte transactions with write back attribute (clean cast outs) will be counted"}, */
{175, 175, "PME_ITA2_BUS_WR_WB_EQ_128BYTE_ANY", "BUS_WR_WB_EQ_128BYTE_ANY"},  /* { "BUS_WR_WB_EQ_128BYTE_ANY", {0x70092}, 0xf0, 1, {0xf00000}, "Bus Write Back Transactions -- CPU or non-CPU (all transactions)./Only cache line transactions with write back or write coalesce attributes will be counted."}, */
{176, 176, "PME_ITA2_BUS_WR_WB_EQ_128BYTE_IO", "BUS_WR_WB_EQ_128BYTE_IO"},  /* { "BUS_WR_WB_EQ_128BYTE_IO", {0x50092}, 0xf0, 1, {0xf00000}, "Bus Write Back Transactions -- non-CPU priority agents/Only cache line transactions with write back or write coalesce attributes will be counted."}, */
{177, 177, "PME_ITA2_BUS_WR_WB_EQ_128BYTE_SELF", "BUS_WR_WB_EQ_128BYTE_SELF"},  /* { "BUS_WR_WB_EQ_128BYTE_SELF", {0x60092}, 0xf0, 1, {0xf00000}, "Bus Write Back Transactions -- local processor/Only cache line transactions with write back or write coalesce attributes will be counted."}, */
{178, 178, "PME_ITA2_CPU_CPL_CHANGES", "CPU_CPL_CHANGES"},  /* { "CPU_CPL_CHANGES", {0x13}, 0xf0, 1, {0xf00000}, "Privilege Level Changes"}, */
{179, 179, "PME_ITA2_CPU_CYCLES", "CPU_CYCLES"},  /* { "CPU_CYCLES", {0x12}, 0xf0, 1, {0xf00000}, "CPU Cycles"}, */
{180, 180, "PME_ITA2_DATA_DEBUG_REGISTER_FAULT", "DATA_DEBUG_REGISTER_FAULT"},  /* { "DATA_DEBUG_REGISTER_FAULT", {0x52}, 0xf0, 1, {0xf00000}, "Fault Due to Data Debug Reg. Match to Load/Store Instruction"}, */
{181, 181, "PME_ITA2_DATA_DEBUG_REGISTER_MATCHES", "DATA_DEBUG_REGISTER_MATCHES"},  /* { "DATA_DEBUG_REGISTER_MATCHES", {0xc6}, 0xf0, 1, {0xf00007}, "Data Debug Register Matches Data Address of Memory Reference."}, */
{182, 182, "PME_ITA2_DATA_EAR_ALAT", "DATA_EAR_ALAT"},  /* { "DATA_EAR_ALAT", {0x6c8}, 0xf0, 1, {0xf00007}, "Data EAR ALAT"}, */
{183, 183, "PME_ITA2_DATA_EAR_CACHE_LAT1024", "DATA_EAR_CACHE_LAT1024"},  /* { "DATA_EAR_CACHE_LAT1024", {0x805c8}, 0xf0, 1, {0xf00007}, "Data EAR Cache -- >= 1024 Cycles"}, */
{184, 184, "PME_ITA2_DATA_EAR_CACHE_LAT128", "DATA_EAR_CACHE_LAT128"},  /* { "DATA_EAR_CACHE_LAT128", {0x505c8}, 0xf0, 1, {0xf00007}, "Data EAR Cache -- >= 128 Cycles"}, */
{185, 185, "PME_ITA2_DATA_EAR_CACHE_LAT16", "DATA_EAR_CACHE_LAT16"},  /* { "DATA_EAR_CACHE_LAT16", {0x205c8}, 0xf0, 1, {0xf00007}, "Data EAR Cache -- >= 16 Cycles"}, */
{186, 186, "PME_ITA2_DATA_EAR_CACHE_LAT2048", "DATA_EAR_CACHE_LAT2048"},  /* { "DATA_EAR_CACHE_LAT2048", {0x905c8}, 0xf0, 1, {0xf00007}, "Data EAR Cache -- >= 2048 Cycles"}, */
{187, 187, "PME_ITA2_DATA_EAR_CACHE_LAT256", "DATA_EAR_CACHE_LAT256"},  /* { "DATA_EAR_CACHE_LAT256", {0x605c8}, 0xf0, 1, {0xf00007}, "Data EAR Cache -- >= 256 Cycles"}, */
{188, 188, "PME_ITA2_DATA_EAR_CACHE_LAT32", "DATA_EAR_CACHE_LAT32"},  /* { "DATA_EAR_CACHE_LAT32", {0x305c8}, 0xf0, 1, {0xf00007}, "Data EAR Cache -- >= 32 Cycles"}, */
{189, 189, "PME_ITA2_DATA_EAR_CACHE_LAT4", "DATA_EAR_CACHE_LAT4"},  /* { "DATA_EAR_CACHE_LAT4", {0x5c8}, 0xf0, 1, {0xf00007}, "Data EAR Cache -- >= 4 Cycles"}, */
{190, 190, "PME_ITA2_DATA_EAR_CACHE_LAT4096", "DATA_EAR_CACHE_LAT4096"},  /* { "DATA_EAR_CACHE_LAT4096", {0xa05c8}, 0xf0, 1, {0xf00007}, "Data EAR Cache -- >= 4096 Cycles"}, */
{191, 191, "PME_ITA2_DATA_EAR_CACHE_LAT512", "DATA_EAR_CACHE_LAT512"},  /* { "DATA_EAR_CACHE_LAT512", {0x705c8}, 0xf0, 1, {0xf00007}, "Data EAR Cache -- >= 512 Cycles"}, */
{192, 192, "PME_ITA2_DATA_EAR_CACHE_LAT64", "DATA_EAR_CACHE_LAT64"},  /* { "DATA_EAR_CACHE_LAT64", {0x405c8}, 0xf0, 1, {0xf00007}, "Data EAR Cache -- >= 64 Cycles"}, */
{193, 193, "PME_ITA2_DATA_EAR_CACHE_LAT8", "DATA_EAR_CACHE_LAT8"},  /* { "DATA_EAR_CACHE_LAT8", {0x105c8}, 0xf0, 1, {0xf00007}, "Data EAR Cache -- >= 8 Cycles"}, */
{194, 194, "PME_ITA2_DATA_EAR_EVENTS", "DATA_EAR_EVENTS"},  /* { "DATA_EAR_EVENTS", {0xc8}, 0xf0, 1, {0xf00007}, "L1 Data Cache EAR Events"}, */
{195, 195, "PME_ITA2_DATA_EAR_TLB_ALL", "DATA_EAR_TLB_ALL"},  /* { "DATA_EAR_TLB_ALL", {0xe04c8}, 0xf0, 1, {0xf00007}, "Data EAR TLB -- All L1 DTLB Misses"}, */
{196, 196, "PME_ITA2_DATA_EAR_TLB_FAULT", "DATA_EAR_TLB_FAULT"},  /* { "DATA_EAR_TLB_FAULT", {0x804c8}, 0xf0, 1, {0xf00007}, "Data EAR TLB -- DTLB Misses which produce a software fault"}, */
{197, 197, "PME_ITA2_DATA_EAR_TLB_L2DTLB", "DATA_EAR_TLB_L2DTLB"},  /* { "DATA_EAR_TLB_L2DTLB", {0x204c8}, 0xf0, 1, {0xf00007}, "Data EAR TLB -- L1 DTLB Misses which hit L2 DTLB"}, */
{198, 198, "PME_ITA2_DATA_EAR_TLB_L2DTLB_OR_FAULT", "DATA_EAR_TLB_L2DTLB_OR_FAULT"},  /* { "DATA_EAR_TLB_L2DTLB_OR_FAULT", {0xa04c8}, 0xf0, 1, {0xf00007}, "Data EAR TLB -- L1 DTLB Misses which hit L2 DTLB or produce a software fault"}, */
{199, 199, "PME_ITA2_DATA_EAR_TLB_L2DTLB_OR_VHPT", "DATA_EAR_TLB_L2DTLB_OR_VHPT"},  /* { "DATA_EAR_TLB_L2DTLB_OR_VHPT", {0x604c8}, 0xf0, 1, {0xf00007}, "Data EAR TLB -- L1 DTLB Misses which hit L2 DTLB or VHPT"}, */
{200, 200, "PME_ITA2_DATA_EAR_TLB_VHPT", "DATA_EAR_TLB_VHPT"},  /* { "DATA_EAR_TLB_VHPT", {0x404c8}, 0xf0, 1, {0xf00007}, "Data EAR TLB -- L1 DTLB Misses which hit VHPT"}, */
{201, 201, "PME_ITA2_DATA_EAR_TLB_VHPT_OR_FAULT", "DATA_EAR_TLB_VHPT_OR_FAULT"},  /* { "DATA_EAR_TLB_VHPT_OR_FAULT", {0xc04c8}, 0xf0, 1, {0xf00007}, "Data EAR TLB -- L1 DTLB Misses which hit VHPT or produce a software fault"}, */
{202, 202, "PME_ITA2_DATA_REFERENCES_SET0", "DATA_REFERENCES_SET0"},  /* { "DATA_REFERENCES_SET0", {0xc3}, 0xf0, 4, {0x5010007}, "Data Memory References Issued to Memory Pipeline"}, */
{203, 203, "PME_ITA2_DATA_REFERENCES_SET1", "DATA_REFERENCES_SET1"},  /* { "DATA_REFERENCES_SET1", {0xc5}, 0xf0, 4, {0x5110007}, "Data Memory References Issued to Memory Pipeline"}, */
{204, 204, "PME_ITA2_DISP_STALLED", "DISP_STALLED"},  /* { "DISP_STALLED", {0x49}, 0xf0, 1, {0xf00000}, "Number of Cycles Dispersal Stalled"}, */
{205, 205, "PME_ITA2_DTLB_INSERTS_HPW", "DTLB_INSERTS_HPW"},  /* { "DTLB_INSERTS_HPW", {0xc9}, 0xf0, 4, {0xf00007}, "Hardware Page Walker Installs to DTLB"}, */
{206, 206, "PME_ITA2_DTLB_INSERTS_HPW_RETIRED", "DTLB_INSERTS_HPW_RETIRED"},  /* { "DTLB_INSERTS_HPW_RETIRED", {0x2c}, 0xf0, 4, {0xf00007}, "VHPT Entries Inserted into DTLB by the Hardware Page Walker"}, */
{207, 207, "PME_ITA2_ENCBR_MISPRED_DETAIL_ALL_ALL_PRED", "ENCBR_MISPRED_DETAIL_ALL_ALL_PRED"},  /* { "ENCBR_MISPRED_DETAIL_ALL_ALL_PRED", {0x63}, 0xf0, 3, {0xf00003}, "Number of Encoded Branches Retired -- All encoded branches regardless of prediction result"}, */
{208, 208, "PME_ITA2_ENCBR_MISPRED_DETAIL_ALL_CORRECT_PRED", "ENCBR_MISPRED_DETAIL_ALL_CORRECT_PRED"},  /* { "ENCBR_MISPRED_DETAIL_ALL_CORRECT_PRED", {0x10063}, 0xf0, 3, {0xf00003}, "Number of Encoded Branches Retired -- All encoded branches, correctly predicted branches (outcome and target)"}, */
{209, 209, "PME_ITA2_ENCBR_MISPRED_DETAIL_ALL_WRONG_PATH", "ENCBR_MISPRED_DETAIL_ALL_WRONG_PATH"},  /* { "ENCBR_MISPRED_DETAIL_ALL_WRONG_PATH", {0x20063}, 0xf0, 3, {0xf00003}, "Number of Encoded Branches Retired -- All encoded branches, mispredicted branches due to wrong branch direction"}, */
{210, 210, "PME_ITA2_ENCBR_MISPRED_DETAIL_ALL_WRONG_TARGET", "ENCBR_MISPRED_DETAIL_ALL_WRONG_TARGET"},  /* { "ENCBR_MISPRED_DETAIL_ALL_WRONG_TARGET", {0x30063}, 0xf0, 3, {0xf00003}, "Number of Encoded Branches Retired -- All encoded branches, mispredicted branches due to wrong target for taken branches"}, */
{211, 211, "PME_ITA2_ENCBR_MISPRED_DETAIL_ALL2_ALL_PRED", "ENCBR_MISPRED_DETAIL_ALL2_ALL_PRED"},  /* { "ENCBR_MISPRED_DETAIL_ALL2_ALL_PRED", {0xc0063}, 0xf0, 3, {0xf00003}, "Number of Encoded Branches Retired -- Only non-return indirect branches, regardless of prediction result"}, */
{212, 212, "PME_ITA2_ENCBR_MISPRED_DETAIL_ALL2_CORRECT_PRED", "ENCBR_MISPRED_DETAIL_ALL2_CORRECT_PRED"},  /* { "ENCBR_MISPRED_DETAIL_ALL2_CORRECT_PRED", {0xd0063}, 0xf0, 3, {0xf00003}, "Number of Encoded Branches Retired -- Only non-return indirect branches, correctly predicted branches (outcome and target)"}, */
{213, 213, "PME_ITA2_ENCBR_MISPRED_DETAIL_ALL2_WRONG_PATH", "ENCBR_MISPRED_DETAIL_ALL2_WRONG_PATH"},  /* { "ENCBR_MISPRED_DETAIL_ALL2_WRONG_PATH", {0xe0063}, 0xf0, 3, {0xf00003}, "Number of Encoded Branches Retired -- Only non-return indirect branches, mispredicted branches due to wrong branch direction"}, */
{214, 214, "PME_ITA2_ENCBR_MISPRED_DETAIL_ALL2_WRONG_TARGET", "ENCBR_MISPRED_DETAIL_ALL2_WRONG_TARGET"},  /* { "ENCBR_MISPRED_DETAIL_ALL2_WRONG_TARGET", {0xf0063}, 0xf0, 3, {0xf00003}, "Number of Encoded Branches Retired -- Only non-return indirect branches, mispredicted branches due to wrong target for taken branches"}, */
{215, 215, "PME_ITA2_ENCBR_MISPRED_DETAIL_OVERSUB_ALL_PRED", "ENCBR_MISPRED_DETAIL_OVERSUB_ALL_PRED"},  /* { "ENCBR_MISPRED_DETAIL_OVERSUB_ALL_PRED", {0x80063}, 0xf0, 3, {0xf00003}, "Number of Encoded Branches Retired -- Only return type branches, regardless of prediction result"}, */
{216, 216, "PME_ITA2_ENCBR_MISPRED_DETAIL_OVERSUB_CORRECT_PRED", "ENCBR_MISPRED_DETAIL_OVERSUB_CORRECT_PRED"},  /* { "ENCBR_MISPRED_DETAIL_OVERSUB_CORRECT_PRED", {0x90063}, 0xf0, 3, {0xf00003}, "Number of Encoded Branches Retired -- Only return type branches, correctly predicted branches (outcome and target)"}, */
{217, 217, "PME_ITA2_ENCBR_MISPRED_DETAIL_OVERSUB_WRONG_PATH", "ENCBR_MISPRED_DETAIL_OVERSUB_WRONG_PATH"},  /* { "ENCBR_MISPRED_DETAIL_OVERSUB_WRONG_PATH", {0xa0063}, 0xf0, 3, {0xf00003}, "Number of Encoded Branches Retired -- Only return type branches, mispredicted branches due to wrong branch direction"}, */
{218, 218, "PME_ITA2_ENCBR_MISPRED_DETAIL_OVERSUB_WRONG_TARGET", "ENCBR_MISPRED_DETAIL_OVERSUB_WRONG_TARGET"},  /* { "ENCBR_MISPRED_DETAIL_OVERSUB_WRONG_TARGET", {0xb0063}, 0xf0, 3, {0xf00003}, "Number of Encoded Branches Retired -- Only return type branches, mispredicted branches due to wrong target for taken branches"}, */
{219, 219, "PME_ITA2_EXTERN_DP_PINS_0_TO_3_ALL", "EXTERN_DP_PINS_0_TO_3_ALL"},  /* { "EXTERN_DP_PINS_0_TO_3_ALL", {0xf009e}, 0xf0, 1, {0xf00000}, "DP Pins 0-3 Asserted -- include pin3 assertion"}, */
{220, 220, "PME_ITA2_EXTERN_DP_PINS_0_TO_3_PIN0", "EXTERN_DP_PINS_0_TO_3_PIN0"},  /* { "EXTERN_DP_PINS_0_TO_3_PIN0", {0x1009e}, 0xf0, 1, {0xf00000}, "DP Pins 0-3 Asserted -- include pin0 assertion"}, */
{221, 221, "PME_ITA2_EXTERN_DP_PINS_0_TO_3_PIN0_OR_PIN1", "EXTERN_DP_PINS_0_TO_3_PIN0_OR_PIN1"},  /* { "EXTERN_DP_PINS_0_TO_3_PIN0_OR_PIN1", {0x3009e}, 0xf0, 1, {0xf00000}, "DP Pins 0-3 Asserted -- include pin0 or pin1 assertion"}, */
{222, 222, "PME_ITA2_EXTERN_DP_PINS_0_TO_3_PIN0_OR_PIN1_OR_PIN2", "EXTERN_DP_PINS_0_TO_3_PIN0_OR_PIN1_OR_PIN2"},  /* { "EXTERN_DP_PINS_0_TO_3_PIN0_OR_PIN1_OR_PIN2", {0x7009e}, 0xf0, 1, {0xf00000}, "DP Pins 0-3 Asserted -- include pin0 or pin1 or pin2 assertion"}, */
{223, 223, "PME_ITA2_EXTERN_DP_PINS_0_TO_3_PIN0_OR_PIN1_OR_PIN3", "EXTERN_DP_PINS_0_TO_3_PIN0_OR_PIN1_OR_PIN3"},  /* { "EXTERN_DP_PINS_0_TO_3_PIN0_OR_PIN1_OR_PIN3", {0xb009e}, 0xf0, 1, {0xf00000}, "DP Pins 0-3 Asserted -- include pin0 or pin1 or pin3 assertion"}, */
{224, 224, "PME_ITA2_EXTERN_DP_PINS_0_TO_3_PIN0_OR_PIN2", "EXTERN_DP_PINS_0_TO_3_PIN0_OR_PIN2"},  /* { "EXTERN_DP_PINS_0_TO_3_PIN0_OR_PIN2", {0x5009e}, 0xf0, 1, {0xf00000}, "DP Pins 0-3 Asserted -- include pin0 or pin2 assertion"}, */
{225, 225, "PME_ITA2_EXTERN_DP_PINS_0_TO_3_PIN0_OR_PIN2_OR_PIN3", "EXTERN_DP_PINS_0_TO_3_PIN0_OR_PIN2_OR_PIN3"},  /* { "EXTERN_DP_PINS_0_TO_3_PIN0_OR_PIN2_OR_PIN3", {0xd009e}, 0xf0, 1, {0xf00000}, "DP Pins 0-3 Asserted -- include pin0 or pin2 or pin3 assertion"}, */
{226, 226, "PME_ITA2_EXTERN_DP_PINS_0_TO_3_PIN0_OR_PIN3", "EXTERN_DP_PINS_0_TO_3_PIN0_OR_PIN3"},  /* { "EXTERN_DP_PINS_0_TO_3_PIN0_OR_PIN3", {0x9009e}, 0xf0, 1, {0xf00000}, "DP Pins 0-3 Asserted -- include pin0 or pin3 assertion"}, */
{227, 227, "PME_ITA2_EXTERN_DP_PINS_0_TO_3_PIN1", "EXTERN_DP_PINS_0_TO_3_PIN1"},  /* { "EXTERN_DP_PINS_0_TO_3_PIN1", {0x2009e}, 0xf0, 1, {0xf00000}, "DP Pins 0-3 Asserted -- include pin1 assertion"}, */
{228, 228, "PME_ITA2_EXTERN_DP_PINS_0_TO_3_PIN1_OR_PIN2", "EXTERN_DP_PINS_0_TO_3_PIN1_OR_PIN2"},  /* { "EXTERN_DP_PINS_0_TO_3_PIN1_OR_PIN2", {0x6009e}, 0xf0, 1, {0xf00000}, "DP Pins 0-3 Asserted -- include pin1 or pin2 assertion"}, */
{229, 229, "PME_ITA2_EXTERN_DP_PINS_0_TO_3_PIN1_OR_PIN2_OR_PIN3", "EXTERN_DP_PINS_0_TO_3_PIN1_OR_PIN2_OR_PIN3"},  /* { "EXTERN_DP_PINS_0_TO_3_PIN1_OR_PIN2_OR_PIN3", {0xe009e}, 0xf0, 1, {0xf00000}, "DP Pins 0-3 Asserted -- include pin1 or pin2 or pin3 assertion"}, */
{230, 230, "PME_ITA2_EXTERN_DP_PINS_0_TO_3_PIN1_OR_PIN3", "EXTERN_DP_PINS_0_TO_3_PIN1_OR_PIN3"},  /* { "EXTERN_DP_PINS_0_TO_3_PIN1_OR_PIN3", {0xa009e}, 0xf0, 1, {0xf00000}, "DP Pins 0-3 Asserted -- include pin1 or pin3 assertion"}, */
{231, 231, "PME_ITA2_EXTERN_DP_PINS_0_TO_3_PIN2", "EXTERN_DP_PINS_0_TO_3_PIN2"},  /* { "EXTERN_DP_PINS_0_TO_3_PIN2", {0x4009e}, 0xf0, 1, {0xf00000}, "DP Pins 0-3 Asserted -- include pin2 assertion"}, */
{232, 232, "PME_ITA2_EXTERN_DP_PINS_0_TO_3_PIN2_OR_PIN3", "EXTERN_DP_PINS_0_TO_3_PIN2_OR_PIN3"},  /* { "EXTERN_DP_PINS_0_TO_3_PIN2_OR_PIN3", {0xc009e}, 0xf0, 1, {0xf00000}, "DP Pins 0-3 Asserted -- include pin2 or pin3 assertion"}, */
{233, 233, "PME_ITA2_EXTERN_DP_PINS_0_TO_3_PIN3", "EXTERN_DP_PINS_0_TO_3_PIN3"},  /* { "EXTERN_DP_PINS_0_TO_3_PIN3", {0x8009e}, 0xf0, 1, {0xf00000}, "DP Pins 0-3 Asserted -- include pin3 assertion"}, */
{234, 234, "PME_ITA2_EXTERN_DP_PINS_4_TO_5_ALL", "EXTERN_DP_PINS_4_TO_5_ALL"},  /* { "EXTERN_DP_PINS_4_TO_5_ALL", {0x3009f}, 0xf0, 1, {0xf00000}, "DP Pins 4-5 Asserted -- include pin5 assertion"}, */
{235, 235, "PME_ITA2_EXTERN_DP_PINS_4_TO_5_PIN4", "EXTERN_DP_PINS_4_TO_5_PIN4"},  /* { "EXTERN_DP_PINS_4_TO_5_PIN4", {0x1009f}, 0xf0, 1, {0xf00000}, "DP Pins 4-5 Asserted -- include pin4 assertion"}, */
{236, 236, "PME_ITA2_EXTERN_DP_PINS_4_TO_5_PIN5", "EXTERN_DP_PINS_4_TO_5_PIN5"},  /* { "EXTERN_DP_PINS_4_TO_5_PIN5", {0x2009f}, 0xf0, 1, {0xf00000}, "DP Pins 4-5 Asserted -- include pin5 assertion"}, */
{237, 237, "PME_ITA2_FE_BUBBLE_ALL", "FE_BUBBLE_ALL"},  /* { "FE_BUBBLE_ALL", {0x71}, 0xf0, 1, {0xf00000}, "Bubbles Seen by FE -- count regardless of cause"}, */
{238, 238, "PME_ITA2_FE_BUBBLE_ALLBUT_FEFLUSH_BUBBLE", "FE_BUBBLE_ALLBUT_FEFLUSH_BUBBLE"},  /* { "FE_BUBBLE_ALLBUT_FEFLUSH_BUBBLE", {0xb0071}, 0xf0, 1, {0xf00000}, "Bubbles Seen by FE -- ALL except FEFLUSH and BUBBLE"}, */
{239, 239, "PME_ITA2_FE_BUBBLE_ALLBUT_IBFULL", "FE_BUBBLE_ALLBUT_IBFULL"},  /* { "FE_BUBBLE_ALLBUT_IBFULL", {0xc0071}, 0xf0, 1, {0xf00000}, "Bubbles Seen by FE -- ALL except IBFULl"}, */
{240, 240, "PME_ITA2_FE_BUBBLE_BRANCH", "FE_BUBBLE_BRANCH"},  /* { "FE_BUBBLE_BRANCH", {0x90071}, 0xf0, 1, {0xf00000}, "Bubbles Seen by FE -- only if caused by any of 4 branch recirculates"}, */
{241, 241, "PME_ITA2_FE_BUBBLE_BUBBLE", "FE_BUBBLE_BUBBLE"},  /* { "FE_BUBBLE_BUBBLE", {0xd0071}, 0xf0, 1, {0xf00000}, "Bubbles Seen by FE -- only if caused by branch bubble stall"}, */
{242, 242, "PME_ITA2_FE_BUBBLE_FEFLUSH", "FE_BUBBLE_FEFLUSH"},  /* { "FE_BUBBLE_FEFLUSH", {0x10071}, 0xf0, 1, {0xf00000}, "Bubbles Seen by FE -- only if caused by a front-end flush"}, */
{243, 243, "PME_ITA2_FE_BUBBLE_FILL_RECIRC", "FE_BUBBLE_FILL_RECIRC"},  /* { "FE_BUBBLE_FILL_RECIRC", {0x80071}, 0xf0, 1, {0xf00000}, "Bubbles Seen by FE -- only if caused by a recirculate for a cache line fill operation"}, */
{244, 244, "PME_ITA2_FE_BUBBLE_GROUP1", "FE_BUBBLE_GROUP1"},  /* { "FE_BUBBLE_GROUP1", {0x30071}, 0xf0, 1, {0xf00000}, "Bubbles Seen by FE -- BUBBLE or BRANCH"}, */
{245, 245, "PME_ITA2_FE_BUBBLE_GROUP2", "FE_BUBBLE_GROUP2"},  /* { "FE_BUBBLE_GROUP2", {0x40071}, 0xf0, 1, {0xf00000}, "Bubbles Seen by FE -- IMISS or TLBMISS"}, */
{246, 246, "PME_ITA2_FE_BUBBLE_GROUP3", "FE_BUBBLE_GROUP3"},  /* { "FE_BUBBLE_GROUP3", {0xa0071}, 0xf0, 1, {0xf00000}, "Bubbles Seen by FE -- FILL_RECIRC or BRANCH"}, */
{247, 247, "PME_ITA2_FE_BUBBLE_IBFULL", "FE_BUBBLE_IBFULL"},  /* { "FE_BUBBLE_IBFULL", {0x50071}, 0xf0, 1, {0xf00000}, "Bubbles Seen by FE -- only if caused by instruction buffer full stall"}, */
{248, 248, "PME_ITA2_FE_BUBBLE_IMISS", "FE_BUBBLE_IMISS"},  /* { "FE_BUBBLE_IMISS", {0x60071}, 0xf0, 1, {0xf00000}, "Bubbles Seen by FE -- only if caused by instruction cache miss stall"}, */
{249, 249, "PME_ITA2_FE_BUBBLE_TLBMISS", "FE_BUBBLE_TLBMISS"},  /* { "FE_BUBBLE_TLBMISS", {0x70071}, 0xf0, 1, {0xf00000}, "Bubbles Seen by FE -- only if caused by TLB stall"}, */
{250, 250, "PME_ITA2_FE_LOST_BW_ALL", "FE_LOST_BW_ALL"},  /* { "FE_LOST_BW_ALL", {0x70}, 0xf0, 2, {0xf00000}, "Invalid Bundles at the Entrance to IB -- count regardless of cause"}, */
{251, 251, "PME_ITA2_FE_LOST_BW_BI", "FE_LOST_BW_BI"},  /* { "FE_LOST_BW_BI", {0x90070}, 0xf0, 2, {0xf00000}, "Invalid Bundles at the Entrance to IB -- only if caused by branch initialization stall"}, */
{252, 252, "PME_ITA2_FE_LOST_BW_BRQ", "FE_LOST_BW_BRQ"},  /* { "FE_LOST_BW_BRQ", {0xa0070}, 0xf0, 2, {0xf00000}, "Invalid Bundles at the Entrance to IB -- only if caused by branch retirement queue stall"}, */
{253, 253, "PME_ITA2_FE_LOST_BW_BR_ILOCK", "FE_LOST_BW_BR_ILOCK"},  /* { "FE_LOST_BW_BR_ILOCK", {0xc0070}, 0xf0, 2, {0xf00000}, "Invalid Bundles at the Entrance to IB -- only if caused by branch interlock stall"}, */
{254, 254, "PME_ITA2_FE_LOST_BW_BUBBLE", "FE_LOST_BW_BUBBLE"},  /* { "FE_LOST_BW_BUBBLE", {0xd0070}, 0xf0, 2, {0xf00000}, "Invalid Bundles at the Entrance to IB -- only if caused by branch resteer bubble stall"}, */
{255, 255, "PME_ITA2_FE_LOST_BW_FEFLUSH", "FE_LOST_BW_FEFLUSH"},  /* { "FE_LOST_BW_FEFLUSH", {0x10070}, 0xf0, 2, {0xf00000}, "Invalid Bundles at the Entrance to IB -- only if caused by a front-end flush"}, */
{256, 256, "PME_ITA2_FE_LOST_BW_FILL_RECIRC", "FE_LOST_BW_FILL_RECIRC"},  /* { "FE_LOST_BW_FILL_RECIRC", {0x80070}, 0xf0, 2, {0xf00000}, "Invalid Bundles at the Entrance to IB -- only if caused by a recirculate for a cache line fill operation"}, */
{257, 257, "PME_ITA2_FE_LOST_BW_IBFULL", "FE_LOST_BW_IBFULL"},  /* { "FE_LOST_BW_IBFULL", {0x50070}, 0xf0, 2, {0xf00000}, "Invalid Bundles at the Entrance to IB -- only if caused by instruction buffer full stall"}, */
{258, 258, "PME_ITA2_FE_LOST_BW_IMISS", "FE_LOST_BW_IMISS"},  /* { "FE_LOST_BW_IMISS", {0x60070}, 0xf0, 2, {0xf00000}, "Invalid Bundles at the Entrance to IB -- only if caused by instruction cache miss stall"}, */
{259, 259, "PME_ITA2_FE_LOST_BW_PLP", "FE_LOST_BW_PLP"},  /* { "FE_LOST_BW_PLP", {0xb0070}, 0xf0, 2, {0xf00000}, "Invalid Bundles at the Entrance to IB -- only if caused by perfect loop prediction stall"}, */
{260, 260, "PME_ITA2_FE_LOST_BW_TLBMISS", "FE_LOST_BW_TLBMISS"},  /* { "FE_LOST_BW_TLBMISS", {0x70070}, 0xf0, 2, {0xf00000}, "Invalid Bundles at the Entrance to IB -- only if caused by TLB stall"}, */
{261, 261, "PME_ITA2_FE_LOST_BW_UNREACHED", "FE_LOST_BW_UNREACHED"},  /* { "FE_LOST_BW_UNREACHED", {0x40070}, 0xf0, 2, {0xf00000}, "Invalid Bundles at the Entrance to IB -- only if caused by unreachable bundle"}, */
{262, 262, "PME_ITA2_FP_FAILED_FCHKF", "FP_FAILED_FCHKF"},  /* { "FP_FAILED_FCHKF", {0x6}, 0xf0, 1, {0xf00001}, "Failed fchkf"}, */
{263, 263, "PME_ITA2_FP_FALSE_SIRSTALL", "FP_FALSE_SIRSTALL"},  /* { "FP_FALSE_SIRSTALL", {0x5}, 0xf0, 1, {0xf00001}, "SIR Stall Without a Trap"}, */
{264, 264, "PME_ITA2_FP_FLUSH_TO_ZERO", "FP_FLUSH_TO_ZERO"},  /* { "FP_FLUSH_TO_ZERO", {0xb}, 0xf0, 2, {0xf00001}, "FP Result Flushed to Zero"}, */
{265, 265, "PME_ITA2_FP_OPS_RETIRED", "FP_OPS_RETIRED"},  /* { "FP_OPS_RETIRED", {0x9}, 0xf0, 4, {0xf00001}, "Retired FP Operations"}, */
{266, 266, "PME_ITA2_FP_TRUE_SIRSTALL", "FP_TRUE_SIRSTALL"},  /* { "FP_TRUE_SIRSTALL", {0x3}, 0xf0, 1, {0xf00001}, "SIR stall asserted and leads to a trap"}, */
{267, 267, "PME_ITA2_HPW_DATA_REFERENCES", "HPW_DATA_REFERENCES"},  /* { "HPW_DATA_REFERENCES", {0x2d}, 0xf0, 4, {0xf00007}, "Data Memory References to VHPT"}, */
{268, 268, "PME_ITA2_IA32_INST_RETIRED", "IA32_INST_RETIRED"},  /* { "IA32_INST_RETIRED", {0x59}, 0xf0, 2, {0xf00000}, "IA-32 Instructions Retired"}, */
{269, 269, "PME_ITA2_IA32_ISA_TRANSITIONS", "IA32_ISA_TRANSITIONS"},  /* { "IA32_ISA_TRANSITIONS", {0x7}, 0xf0, 1, {0xf00000}, "IA-64 to/from IA-32 ISA Transitions"}, */
{270, 270, "PME_ITA2_IA64_INST_RETIRED", "IA64_INST_RETIRED"},  /* { "IA64_INST_RETIRED", {0x8}, 0xf0, 6, {0xf00003}, "Retired IA-64 Instructions, alias to IA64_INST_RETIRED_THIS"}, */
{271, 271, "PME_ITA2_IA64_INST_RETIRED_THIS", "IA64_INST_RETIRED_THIS"},  /* { "IA64_INST_RETIRED_THIS", {0x8}, 0xf0, 6, {0xf00003}, "Retired IA-64 Instructions -- Retired IA-64 Instructions"}, */
{272, 272, "PME_ITA2_IA64_TAGGED_INST_RETIRED_IBRP0_PMC8", "IA64_TAGGED_INST_RETIRED_IBRP0_PMC8"},  /* { "IA64_TAGGED_INST_RETIRED_IBRP0_PMC8", {0x8}, 0xf0, 6, {0xf00003}, "Retired Tagged Instructions -- Instruction tagged by Instruction Breakpoint Pair 0 and opcode matcher PMC8. Code executed with PSR.is=1 is included."}, */
{273, 273, "PME_ITA2_IA64_TAGGED_INST_RETIRED_IBRP1_PMC9", "IA64_TAGGED_INST_RETIRED_IBRP1_PMC9"},  /* { "IA64_TAGGED_INST_RETIRED_IBRP1_PMC9", {0x10008}, 0xf0, 6, {0xf00003}, "Retired Tagged Instructions -- Instruction tagged by Instruction Breakpoint Pair 1 and opcode matcher PMC9. Code executed with PSR.is=1 is included."}, */
{274, 274, "PME_ITA2_IA64_TAGGED_INST_RETIRED_IBRP2_PMC8", "IA64_TAGGED_INST_RETIRED_IBRP2_PMC8"},  /* { "IA64_TAGGED_INST_RETIRED_IBRP2_PMC8", {0x20008}, 0xf0, 6, {0xf00003}, "Retired Tagged Instructions -- Instruction tagged by Instruction Breakpoint Pair 2 and opcode matcher PMC8. Code executed with PSR.is=1 is not included."}, */
{275, 275, "PME_ITA2_IA64_TAGGED_INST_RETIRED_IBRP3_PMC9", "IA64_TAGGED_INST_RETIRED_IBRP3_PMC9"},  /* { "IA64_TAGGED_INST_RETIRED_IBRP3_PMC9", {0x30008}, 0xf0, 6, {0xf00003}, "Retired Tagged Instructions -- Instruction tagged by Instruction Breakpoint Pair 3 and opcode matcher PMC9. Code executed with PSR.is=1 is not included."}, */
{276, 276, "PME_ITA2_IDEAL_BE_LOST_BW_DUE_TO_FE_ALL", "IDEAL_BE_LOST_BW_DUE_TO_FE_ALL"},  /* { "IDEAL_BE_LOST_BW_DUE_TO_FE_ALL", {0x73}, 0xf0, 2, {0xf00000}, "Invalid Bundles at the Exit from IB -- count regardless of cause"}, */
{277, 277, "PME_ITA2_IDEAL_BE_LOST_BW_DUE_TO_FE_BI", "IDEAL_BE_LOST_BW_DUE_TO_FE_BI"},  /* { "IDEAL_BE_LOST_BW_DUE_TO_FE_BI", {0x90073}, 0xf0, 2, {0xf00000}, "Invalid Bundles at the Exit from IB -- only if caused by branch initialization stall"}, */
{278, 278, "PME_ITA2_IDEAL_BE_LOST_BW_DUE_TO_FE_BRQ", "IDEAL_BE_LOST_BW_DUE_TO_FE_BRQ"},  /* { "IDEAL_BE_LOST_BW_DUE_TO_FE_BRQ", {0xa0073}, 0xf0, 2, {0xf00000}, "Invalid Bundles at the Exit from IB -- only if caused by branch retirement queue stall"}, */
{279, 279, "PME_ITA2_IDEAL_BE_LOST_BW_DUE_TO_FE_BR_ILOCK", "IDEAL_BE_LOST_BW_DUE_TO_FE_BR_ILOCK"},  /* { "IDEAL_BE_LOST_BW_DUE_TO_FE_BR_ILOCK", {0xc0073}, 0xf0, 2, {0xf00000}, "Invalid Bundles at the Exit from IB -- only if caused by branch interlock stall"}, */
{280, 280, "PME_ITA2_IDEAL_BE_LOST_BW_DUE_TO_FE_BUBBLE", "IDEAL_BE_LOST_BW_DUE_TO_FE_BUBBLE"},  /* { "IDEAL_BE_LOST_BW_DUE_TO_FE_BUBBLE", {0xd0073}, 0xf0, 2, {0xf00000}, "Invalid Bundles at the Exit from IB -- only if caused by branch resteer bubble stall"}, */
{281, 281, "PME_ITA2_IDEAL_BE_LOST_BW_DUE_TO_FE_FEFLUSH", "IDEAL_BE_LOST_BW_DUE_TO_FE_FEFLUSH"},  /* { "IDEAL_BE_LOST_BW_DUE_TO_FE_FEFLUSH", {0x10073}, 0xf0, 2, {0xf00000}, "Invalid Bundles at the Exit from IB -- only if caused by a front-end flush"}, */
{282, 282, "PME_ITA2_IDEAL_BE_LOST_BW_DUE_TO_FE_FILL_RECIRC", "IDEAL_BE_LOST_BW_DUE_TO_FE_FILL_RECIRC"},  /* { "IDEAL_BE_LOST_BW_DUE_TO_FE_FILL_RECIRC", {0x80073}, 0xf0, 2, {0xf00000}, "Invalid Bundles at the Exit from IB -- only if caused by a recirculate for a cache line fill operation"}, */
{283, 283, "PME_ITA2_IDEAL_BE_LOST_BW_DUE_TO_FE_IBFULL", "IDEAL_BE_LOST_BW_DUE_TO_FE_IBFULL"},  /* { "IDEAL_BE_LOST_BW_DUE_TO_FE_IBFULL", {0x50073}, 0xf0, 2, {0xf00000}, "Invalid Bundles at the Exit from IB -- (* meaningless for this event *)"}, */
{284, 284, "PME_ITA2_IDEAL_BE_LOST_BW_DUE_TO_FE_IMISS", "IDEAL_BE_LOST_BW_DUE_TO_FE_IMISS"},  /* { "IDEAL_BE_LOST_BW_DUE_TO_FE_IMISS", {0x60073}, 0xf0, 2, {0xf00000}, "Invalid Bundles at the Exit from IB -- only if caused by instruction cache miss stall"}, */
{285, 285, "PME_ITA2_IDEAL_BE_LOST_BW_DUE_TO_FE_PLP", "IDEAL_BE_LOST_BW_DUE_TO_FE_PLP"},  /* { "IDEAL_BE_LOST_BW_DUE_TO_FE_PLP", {0xb0073}, 0xf0, 2, {0xf00000}, "Invalid Bundles at the Exit from IB -- only if caused by perfect loop prediction stall"}, */
{286, 286, "PME_ITA2_IDEAL_BE_LOST_BW_DUE_TO_FE_TLBMISS", "IDEAL_BE_LOST_BW_DUE_TO_FE_TLBMISS"},  /* { "IDEAL_BE_LOST_BW_DUE_TO_FE_TLBMISS", {0x70073}, 0xf0, 2, {0xf00000}, "Invalid Bundles at the Exit from IB -- only if caused by TLB stall"}, */
{287, 287, "PME_ITA2_IDEAL_BE_LOST_BW_DUE_TO_FE_UNREACHED", "IDEAL_BE_LOST_BW_DUE_TO_FE_UNREACHED"},  /* { "IDEAL_BE_LOST_BW_DUE_TO_FE_UNREACHED", {0x40073}, 0xf0, 2, {0xf00000}, "Invalid Bundles at the Exit from IB -- only if caused by unreachable bundle"}, */
{288, 288, "PME_ITA2_INST_CHKA_LDC_ALAT_ALL", "INST_CHKA_LDC_ALAT_ALL"},  /* { "INST_CHKA_LDC_ALAT_ALL", {0x30056}, 0xf0, 2, {0xf00007}, "Retired chk.a and ld.c Instructions -- both integer and floating point instructions"}, */
{289, 289, "PME_ITA2_INST_CHKA_LDC_ALAT_FP", "INST_CHKA_LDC_ALAT_FP"},  /* { "INST_CHKA_LDC_ALAT_FP", {0x20056}, 0xf0, 2, {0xf00007}, "Retired chk.a and ld.c Instructions -- only floating point instructions"}, */
{290, 290, "PME_ITA2_INST_CHKA_LDC_ALAT_INT", "INST_CHKA_LDC_ALAT_INT"},  /* { "INST_CHKA_LDC_ALAT_INT", {0x10056}, 0xf0, 2, {0xf00007}, "Retired chk.a and ld.c Instructions -- only integer instructions"}, */
{291, 291, "PME_ITA2_INST_DISPERSED", "INST_DISPERSED"},  /* { "INST_DISPERSED", {0x4d}, 0xf0, 6, {0xf00001}, "Syllables Dispersed from REN to REG stage"}, */
{292, 292, "PME_ITA2_INST_FAILED_CHKA_LDC_ALAT_ALL", "INST_FAILED_CHKA_LDC_ALAT_ALL"},  /* { "INST_FAILED_CHKA_LDC_ALAT_ALL", {0x30057}, 0xf0, 1, {0xf00007}, "Failed chk.a and ld.c Instructions -- both integer and floating point instructions"}, */
{293, 293, "PME_ITA2_INST_FAILED_CHKA_LDC_ALAT_FP", "INST_FAILED_CHKA_LDC_ALAT_FP"},  /* { "INST_FAILED_CHKA_LDC_ALAT_FP", {0x20057}, 0xf0, 1, {0xf00007}, "Failed chk.a and ld.c Instructions -- only floating point instructions"}, */
{294, 294, "PME_ITA2_INST_FAILED_CHKA_LDC_ALAT_INT", "INST_FAILED_CHKA_LDC_ALAT_INT"},  /* { "INST_FAILED_CHKA_LDC_ALAT_INT", {0x10057}, 0xf0, 1, {0xf00007}, "Failed chk.a and ld.c Instructions -- only integer instructions"}, */
{295, 295, "PME_ITA2_INST_FAILED_CHKS_RETIRED_ALL", "INST_FAILED_CHKS_RETIRED_ALL"},  /* { "INST_FAILED_CHKS_RETIRED_ALL", {0x30055}, 0xf0, 1, {0xf00000}, "Failed chk.s Instructions -- both integer and floating point instructions"}, */
{296, 296, "PME_ITA2_INST_FAILED_CHKS_RETIRED_FP", "INST_FAILED_CHKS_RETIRED_FP"},  /* { "INST_FAILED_CHKS_RETIRED_FP", {0x20055}, 0xf0, 1, {0xf00000}, "Failed chk.s Instructions -- only floating point instructions"}, */
{297, 297, "PME_ITA2_INST_FAILED_CHKS_RETIRED_INT", "INST_FAILED_CHKS_RETIRED_INT"},  /* { "INST_FAILED_CHKS_RETIRED_INT", {0x10055}, 0xf0, 1, {0xf00000}, "Failed chk.s Instructions -- only integer instructions"}, */
{298, 298, "PME_ITA2_ISB_BUNPAIRS_IN", "ISB_BUNPAIRS_IN"},  /* { "ISB_BUNPAIRS_IN", {0x46}, 0xf0, 1, {0xf00001}, "Bundle Pairs Written from L2 into FE"}, */
{299, 299, "PME_ITA2_ITLB_MISSES_FETCH_ALL", "ITLB_MISSES_FETCH_ALL"},  /* { "ITLB_MISSES_FETCH_ALL", {0x30047}, 0xf0, 1, {0xf00001}, "ITLB Misses Demand Fetch -- All tlb misses will be counted. Note that this is not equal to sum of the L1ITLB and L2ITLB umasks because any access could be a miss in L1ITLB and L2ITLB."}, */
{300, 300, "PME_ITA2_ITLB_MISSES_FETCH_L1ITLB", "ITLB_MISSES_FETCH_L1ITLB"},  /* { "ITLB_MISSES_FETCH_L1ITLB", {0x10047}, 0xf0, 1, {0xf00001}, "ITLB Misses Demand Fetch -- All misses in L1ITLB will be counted. even if L1ITLB is not updated for an access (Uncacheable/nat page/not present page/faulting/some flushed), it will be counted here."}, */
{301, 301, "PME_ITA2_ITLB_MISSES_FETCH_L2ITLB", "ITLB_MISSES_FETCH_L2ITLB"},  /* { "ITLB_MISSES_FETCH_L2ITLB", {0x20047}, 0xf0, 1, {0xf00001}, "ITLB Misses Demand Fetch -- All misses in L1ITLB which also missed in L2ITLB will be counted."}, */
{302, 302, "PME_ITA2_L1DTLB_TRANSFER", "L1DTLB_TRANSFER"},  /* { "L1DTLB_TRANSFER", {0xc0}, 0xf0, 1, {0x5010007}, "L1DTLB Misses That Hit in the L2DTLB for Accesses Counted in L1D_READS"}, */
{303, 303, "PME_ITA2_L1D_READS_SET0", "L1D_READS_SET0"},  /* { "L1D_READS_SET0", {0xc2}, 0xf0, 2, {0x5010007}, "L1 Data Cache Reads"}, */
{304, 304, "PME_ITA2_L1D_READS_SET1", "L1D_READS_SET1"},  /* { "L1D_READS_SET1", {0xc4}, 0xf0, 2, {0x5110007}, "L1 Data Cache Reads"}, */
{305, 305, "PME_ITA2_L1D_READ_MISSES_ALL", "L1D_READ_MISSES_ALL"},  /* { "L1D_READ_MISSES_ALL", {0xc7}, 0xf0, 2, {0x5110007}, "L1 Data Cache Read Misses -- all L1D read misses will be counted."}, */
{306, 306, "PME_ITA2_L1D_READ_MISSES_RSE_FILL", "L1D_READ_MISSES_RSE_FILL"},  /* { "L1D_READ_MISSES_RSE_FILL", {0x100c7}, 0xf0, 2, {0x5110007}, "L1 Data Cache Read Misses -- only L1D read misses caused by RSE fills will be counted"}, */
{307, 307, "PME_ITA2_L1ITLB_INSERTS_HPW", "L1ITLB_INSERTS_HPW"},  /* { "L1ITLB_INSERTS_HPW", {0x48}, 0xf0, 1, {0xf00001}, "L1ITLB Hardware Page Walker Inserts"}, */
{308, 308, "PME_ITA2_L1I_EAR_CACHE_LAT0", "L1I_EAR_CACHE_LAT0"},  /* { "L1I_EAR_CACHE_LAT0", {0x400343}, 0xf0, 1, {0xf00001}, "L1I EAR Cache -- > 0 Cycles (All L1 Misses)"}, */
{309, 309, "PME_ITA2_L1I_EAR_CACHE_LAT1024", "L1I_EAR_CACHE_LAT1024"},  /* { "L1I_EAR_CACHE_LAT1024", {0xc00343}, 0xf0, 1, {0xf00001}, "L1I EAR Cache -- >= 1024 Cycles"}, */
{310, 310, "PME_ITA2_L1I_EAR_CACHE_LAT128", "L1I_EAR_CACHE_LAT128"},  /* { "L1I_EAR_CACHE_LAT128", {0xf00343}, 0xf0, 1, {0xf00001}, "L1I EAR Cache -- >= 128 Cycles"}, */
{311, 311, "PME_ITA2_L1I_EAR_CACHE_LAT16", "L1I_EAR_CACHE_LAT16"},  /* { "L1I_EAR_CACHE_LAT16", {0xfc0343}, 0xf0, 1, {0xf00001}, "L1I EAR Cache -- >= 16 Cycles"}, */
{312, 312, "PME_ITA2_L1I_EAR_CACHE_LAT256", "L1I_EAR_CACHE_LAT256"},  /* { "L1I_EAR_CACHE_LAT256", {0xe00343}, 0xf0, 1, {0xf00001}, "L1I EAR Cache -- >= 256 Cycles"}, */
{313, 313, "PME_ITA2_L1I_EAR_CACHE_LAT32", "L1I_EAR_CACHE_LAT32"},  /* { "L1I_EAR_CACHE_LAT32", {0xf80343}, 0xf0, 1, {0xf00001}, "L1I EAR Cache -- >= 32 Cycles"}, */
{314, 314, "PME_ITA2_L1I_EAR_CACHE_LAT4", "L1I_EAR_CACHE_LAT4"},  /* { "L1I_EAR_CACHE_LAT4", {0xff0343}, 0xf0, 1, {0xf00001}, "L1I EAR Cache -- >= 4 Cycles"}, */
{315, 315, "PME_ITA2_L1I_EAR_CACHE_LAT4096", "L1I_EAR_CACHE_LAT4096"},  /* { "L1I_EAR_CACHE_LAT4096", {0x800343}, 0xf0, 1, {0xf00001}, "L1I EAR Cache -- >= 4096 Cycles"}, */
{316, 316, "PME_ITA2_L1I_EAR_CACHE_LAT8", "L1I_EAR_CACHE_LAT8"},  /* { "L1I_EAR_CACHE_LAT8", {0xfe0343}, 0xf0, 1, {0xf00001}, "L1I EAR Cache -- >= 8 Cycles"}, */
{317, 317, "PME_ITA2_L1I_EAR_CACHE_RAB", "L1I_EAR_CACHE_RAB"},  /* { "L1I_EAR_CACHE_RAB", {0x343}, 0xf0, 1, {0xf00001}, "L1I EAR Cache -- RAB HIT"}, */
{318, 318, "PME_ITA2_L1I_EAR_EVENTS", "L1I_EAR_EVENTS"},  /* { "L1I_EAR_EVENTS", {0x43}, 0xf0, 1, {0xf00001}, "Instruction EAR Events"}, */
{319, 319, "PME_ITA2_L1I_EAR_TLB_ALL", "L1I_EAR_TLB_ALL"},  /* { "L1I_EAR_TLB_ALL", {0x70243}, 0xf0, 1, {0xf00001}, "L1I EAR TLB -- All L1 ITLB Misses"}, */
{320, 320, "PME_ITA2_L1I_EAR_TLB_FAULT", "L1I_EAR_TLB_FAULT"},  /* { "L1I_EAR_TLB_FAULT", {0x40243}, 0xf0, 1, {0xf00001}, "L1I EAR TLB -- ITLB Misses which produced a fault"}, */
{321, 321, "PME_ITA2_L1I_EAR_TLB_L2TLB", "L1I_EAR_TLB_L2TLB"},  /* { "L1I_EAR_TLB_L2TLB", {0x10243}, 0xf0, 1, {0xf00001}, "L1I EAR TLB -- L1 ITLB Misses which hit L2 ITLB"}, */
{322, 322, "PME_ITA2_L1I_EAR_TLB_L2TLB_OR_FAULT", "L1I_EAR_TLB_L2TLB_OR_FAULT"},  /* { "L1I_EAR_TLB_L2TLB_OR_FAULT", {0x50243}, 0xf0, 1, {0xf00001}, "L1I EAR TLB -- L1 ITLB Misses which hit L2 ITLB or produce a software fault"}, */
{323, 323, "PME_ITA2_L1I_EAR_TLB_L2TLB_OR_VHPT", "L1I_EAR_TLB_L2TLB_OR_VHPT"},  /* { "L1I_EAR_TLB_L2TLB_OR_VHPT", {0x30243}, 0xf0, 1, {0xf00001}, "L1I EAR TLB -- L1 ITLB Misses which hit L2 ITLB or VHPT"}, */
{324, 324, "PME_ITA2_L1I_EAR_TLB_VHPT", "L1I_EAR_TLB_VHPT"},  /* { "L1I_EAR_TLB_VHPT", {0x20243}, 0xf0, 1, {0xf00001}, "L1I EAR TLB -- L1 ITLB Misses which hit VHPT"}, */
{325, 325, "PME_ITA2_L1I_EAR_TLB_VHPT_OR_FAULT", "L1I_EAR_TLB_VHPT_OR_FAULT"},  /* { "L1I_EAR_TLB_VHPT_OR_FAULT", {0x60243}, 0xf0, 1, {0xf00001}, "L1I EAR TLB -- L1 ITLB Misses which hit VHPT or produce a software fault"}, */
{326, 326, "PME_ITA2_L1I_FETCH_ISB_HIT", "L1I_FETCH_ISB_HIT"},  /* { "L1I_FETCH_ISB_HIT", {0x66}, 0xf0, 1, {0xf00001}, "\"Just-In-Time\" Instruction Fetch Hitting in and Being Bypassed from ISB"}, */
{327, 327, "PME_ITA2_L1I_FETCH_RAB_HIT", "L1I_FETCH_RAB_HIT"},  /* { "L1I_FETCH_RAB_HIT", {0x65}, 0xf0, 1, {0xf00001}, "Instruction Fetch Hitting in RAB"}, */
{328, 328, "PME_ITA2_L1I_FILLS", "L1I_FILLS"},  /* { "L1I_FILLS", {0x41}, 0xf0, 1, {0xf00001}, "L1 Instruction Cache Fills"}, */
{329, 329, "PME_ITA2_L1I_PREFETCHES", "L1I_PREFETCHES"},  /* { "L1I_PREFETCHES", {0x44}, 0xf0, 1, {0xf00001}, "L1 Instruction Prefetch Requests"}, */
{330, 330, "PME_ITA2_L1I_PREFETCH_STALL_ALL", "L1I_PREFETCH_STALL_ALL"},  /* { "L1I_PREFETCH_STALL_ALL", {0x30067}, 0xf0, 1, {0xf00000}, "Prefetch Pipeline Stalls -- Number of clocks prefetch pipeline is stalled"}, */
{331, 331, "PME_ITA2_L1I_PREFETCH_STALL_FLOW", "L1I_PREFETCH_STALL_FLOW"},  /* { "L1I_PREFETCH_STALL_FLOW", {0x20067}, 0xf0, 1, {0xf00000}, "Prefetch Pipeline Stalls -- Number of clocks flow is not asserted"}, */
{332, 332, "PME_ITA2_L1I_PURGE", "L1I_PURGE"},  /* { "L1I_PURGE", {0x4b}, 0xf0, 1, {0xf00001}, "L1ITLB Purges Handled by L1I"}, */
{333, 333, "PME_ITA2_L1I_PVAB_OVERFLOW", "L1I_PVAB_OVERFLOW"},  /* { "L1I_PVAB_OVERFLOW", {0x69}, 0xf0, 1, {0xf00000}, "PVAB Overflow"}, */
{334, 334, "PME_ITA2_L1I_RAB_ALMOST_FULL", "L1I_RAB_ALMOST_FULL"},  /* { "L1I_RAB_ALMOST_FULL", {0x64}, 0xf0, 1, {0xf00000}, "Is RAB Almost Full?"}, */
{335, 335, "PME_ITA2_L1I_RAB_FULL", "L1I_RAB_FULL"},  /* { "L1I_RAB_FULL", {0x60}, 0xf0, 1, {0xf00000}, "Is RAB Full?"}, */
{336, 336, "PME_ITA2_L1I_READS", "L1I_READS"},  /* { "L1I_READS", {0x40}, 0xf0, 1, {0xf00001}, "L1 Instruction Cache Reads"}, */
{337, 337, "PME_ITA2_L1I_SNOOP", "L1I_SNOOP"},  /* { "L1I_SNOOP", {0x4a}, 0xf0, 1, {0xf00007}, "Snoop Requests Handled by L1I"}, */
{338, 338, "PME_ITA2_L1I_STRM_PREFETCHES", "L1I_STRM_PREFETCHES"},  /* { "L1I_STRM_PREFETCHES", {0x5f}, 0xf0, 1, {0xf00001}, "L1 Instruction Cache Line Prefetch Requests"}, */
{339, 339, "PME_ITA2_L2DTLB_MISSES", "L2DTLB_MISSES"},  /* { "L2DTLB_MISSES", {0xc1}, 0xf0, 4, {0x5010007}, "L2DTLB Misses"}, */
{340, 340, "PME_ITA2_L2_BAD_LINES_SELECTED_ANY", "L2_BAD_LINES_SELECTED_ANY"},  /* { "L2_BAD_LINES_SELECTED_ANY", {0xb9}, 0xf0, 4, {0x4320007}, "Valid Line Replaced When Invalid Line Is Available -- Valid line replaced when invalid line is available"}, */
{341, 341, "PME_ITA2_L2_BYPASS_L2_DATA1", "L2_BYPASS_L2_DATA1"},  /* { "L2_BYPASS_L2_DATA1", {0xb8}, 0xf0, 1, {0x4320007}, "Count L2 Bypasses -- Count only L2 data bypasses (L1D to L2A)"}, */
{342, 342, "PME_ITA2_L2_BYPASS_L2_DATA2", "L2_BYPASS_L2_DATA2"},  /* { "L2_BYPASS_L2_DATA2", {0x100b8}, 0xf0, 1, {0x4320007}, "Count L2 Bypasses -- Count only L2 data bypasses (L1W to L2I)"}, */
{343, 343, "PME_ITA2_L2_BYPASS_L2_INST1", "L2_BYPASS_L2_INST1"},  /* { "L2_BYPASS_L2_INST1", {0x400b8}, 0xf0, 1, {0x4320007}, "Count L2 Bypasses -- Count only L2 instruction bypasses (L1D to L2A)"}, */
{344, 344, "PME_ITA2_L2_BYPASS_L2_INST2", "L2_BYPASS_L2_INST2"},  /* { "L2_BYPASS_L2_INST2", {0x500b8}, 0xf0, 1, {0x4320007}, "Count L2 Bypasses -- Count only L2 instruction bypasses (L1W to L2I)"}, */
{345, 345, "PME_ITA2_L2_BYPASS_L3_DATA1", "L2_BYPASS_L3_DATA1"},  /* { "L2_BYPASS_L3_DATA1", {0x200b8}, 0xf0, 1, {0x4320007}, "Count L2 Bypasses -- Count only L3 data bypasses (L1D to L2A)"}, */
{346, 346, "PME_ITA2_L2_BYPASS_L3_INST1", "L2_BYPASS_L3_INST1"},  /* { "L2_BYPASS_L3_INST1", {0x600b8}, 0xf0, 1, {0x4320007}, "Count L2 Bypasses -- Count only L3 instruction bypasses (L1D to L2A)"}, */
{347, 347, "PME_ITA2_L2_DATA_REFERENCES_L2_ALL", "L2_DATA_REFERENCES_L2_ALL"},  /* { "L2_DATA_REFERENCES_L2_ALL", {0x300b2}, 0xf0, 4, {0x4120007}, "Data Read/Write Access to L2 -- count both read and write operations (semaphores will count as 2)"}, */
{348, 348, "PME_ITA2_L2_DATA_REFERENCES_L2_DATA_READS", "L2_DATA_REFERENCES_L2_DATA_READS"},  /* { "L2_DATA_REFERENCES_L2_DATA_READS", {0x100b2}, 0xf0, 4, {0x4120007}, "Data Read/Write Access to L2 -- count only data read and semaphore operations."}, */
{349, 349, "PME_ITA2_L2_DATA_REFERENCES_L2_DATA_WRITES", "L2_DATA_REFERENCES_L2_DATA_WRITES"},  /* { "L2_DATA_REFERENCES_L2_DATA_WRITES", {0x200b2}, 0xf0, 4, {0x4120007}, "Data Read/Write Access to L2 -- count only data write and semaphore operations"}, */
{350, 350, "PME_ITA2_L2_FILLB_FULL_THIS", "L2_FILLB_FULL_THIS"},  /* { "L2_FILLB_FULL_THIS", {0xbf}, 0xf0, 1, {0x4520000}, "L2D Fill Buffer Is Full -- L2 Fill buffer is full"}, */
{351, 351, "PME_ITA2_L2_FORCE_RECIRC_ANY", "L2_FORCE_RECIRC_ANY"},  /* { "L2_FORCE_RECIRC_ANY", {0xb4}, 0xf0, 4, {0x4220007}, "Forced Recirculates -- count forced recirculates regardless of cause. SMC_HIT, TRAN_PREF & SNP_OR_L3 will not be included here."}, */
{352, 352, "PME_ITA2_L2_FORCE_RECIRC_FILL_HIT", "L2_FORCE_RECIRC_FILL_HIT"},  /* { "L2_FORCE_RECIRC_FILL_HIT", {0x900b4}, 0xf0, 4, {0x4220007}, "Forced Recirculates -- count only those caused by an L2 miss which hit in the fill buffer."}, */
{353, 353, "PME_ITA2_L2_FORCE_RECIRC_FRC_RECIRC", "L2_FORCE_RECIRC_FRC_RECIRC"},  /* { "L2_FORCE_RECIRC_FRC_RECIRC", {0xe00b4}, 0xf0, 4, {0x4220007}, "Forced Recirculates -- caused by an L2 miss when a force recirculate already existed"}, */
{354, 354, "PME_ITA2_L2_FORCE_RECIRC_IPF_MISS", "L2_FORCE_RECIRC_IPF_MISS"},  /* { "L2_FORCE_RECIRC_IPF_MISS", {0xa00b4}, 0xf0, 4, {0x4220007}, "Forced Recirculates -- caused by L2 miss when instruction prefetch buffer miss already existed"}, */
{355, 355, "PME_ITA2_L2_FORCE_RECIRC_L1W", "L2_FORCE_RECIRC_L1W"},  /* { "L2_FORCE_RECIRC_L1W", {0x200b4}, 0xf0, 4, {0x4220007}, "Forced Recirculates -- count only those caused by forced limbo"}, */
{356, 356, "PME_ITA2_L2_FORCE_RECIRC_OZQ_MISS", "L2_FORCE_RECIRC_OZQ_MISS"},  /* { "L2_FORCE_RECIRC_OZQ_MISS", {0xc00b4}, 0xf0, 4, {0x4220007}, "Forced Recirculates -- caused by an L2 miss when an OZQ miss already existed"}, */
{357, 357, "PME_ITA2_L2_FORCE_RECIRC_SAME_INDEX", "L2_FORCE_RECIRC_SAME_INDEX"},  /* { "L2_FORCE_RECIRC_SAME_INDEX", {0xd00b4}, 0xf0, 4, {0x4220007}, "Forced Recirculates -- caused by an L2 miss when a miss to the same index already existed"}, */
{358, 358, "PME_ITA2_L2_FORCE_RECIRC_SMC_HIT", "L2_FORCE_RECIRC_SMC_HIT"},  /* { "L2_FORCE_RECIRC_SMC_HIT", {0x100b4}, 0xf0, 4, {0x4220007}, "Forced Recirculates -- count only those caused by SMC hits due to an ifetch and load to same cache line or a pending WT store"}, */
{359, 359, "PME_ITA2_L2_FORCE_RECIRC_SNP_OR_L3", "L2_FORCE_RECIRC_SNP_OR_L3"},  /* { "L2_FORCE_RECIRC_SNP_OR_L3", {0x600b4}, 0xf0, 4, {0x4220007}, "Forced Recirculates -- count only those caused by a snoop or L3 issue"}, */
{360, 360, "PME_ITA2_L2_FORCE_RECIRC_TAG_NOTOK", "L2_FORCE_RECIRC_TAG_NOTOK"},  /* { "L2_FORCE_RECIRC_TAG_NOTOK", {0x400b4}, 0xf0, 4, {0x4220007}, "Forced Recirculates -- count only those caused by L2 hits caused by in flight snoops, stores with a sibling miss to the same index, sibling probe to the same line or pending sync.ia instructions."}, */
{361, 361, "PME_ITA2_L2_FORCE_RECIRC_TRAN_PREF", "L2_FORCE_RECIRC_TRAN_PREF"},  /* { "L2_FORCE_RECIRC_TRAN_PREF", {0x500b4}, 0xf0, 4, {0x4220007}, "Forced Recirculates -- count only those caused by transforms to prefetches"}, */
{362, 362, "PME_ITA2_L2_FORCE_RECIRC_VIC_BUF_FULL", "L2_FORCE_RECIRC_VIC_BUF_FULL"},  /* { "L2_FORCE_RECIRC_VIC_BUF_FULL", {0xb00b4}, 0xf0, 4, {0x4220007}, "Forced Recirculates -- count only those caused by an L2 miss with victim buffer full"}, */
{363, 363, "PME_ITA2_L2_FORCE_RECIRC_VIC_PEND", "L2_FORCE_RECIRC_VIC_PEND"},  /* { "L2_FORCE_RECIRC_VIC_PEND", {0x800b4}, 0xf0, 4, {0x4220007}, "Forced Recirculates -- count only those caused by an L2 miss with pending victim"}, */
{364, 364, "PME_ITA2_L2_GOT_RECIRC_IFETCH_ANY", "L2_GOT_RECIRC_IFETCH_ANY"},  /* { "L2_GOT_RECIRC_IFETCH_ANY", {0x800ba}, 0xf0, 1, {0x4420007}, "Instruction Fetch Recirculates Received by L2D -- Instruction fetch recirculates received by L2"}, */
{365, 365, "PME_ITA2_L2_GOT_RECIRC_OZQ_ACC", "L2_GOT_RECIRC_OZQ_ACC"},  /* { "L2_GOT_RECIRC_OZQ_ACC", {0xb6}, 0xf0, 1, {0x4220007}, "Counts Number of OZQ Accesses Recirculated to L1D"}, */
{366, 366, "PME_ITA2_L2_IFET_CANCELS_ANY", "L2_IFET_CANCELS_ANY"},  /* { "L2_IFET_CANCELS_ANY", {0xa1}, 0xf0, 1, {0x4020007}, "Instruction Fetch Cancels by the L2 -- total instruction fetch cancels by L2"}, */
{367, 367, "PME_ITA2_L2_IFET_CANCELS_BYPASS", "L2_IFET_CANCELS_BYPASS"},  /* { "L2_IFET_CANCELS_BYPASS", {0x200a1}, 0xf0, 1, {0x4020007}, "Instruction Fetch Cancels by the L2 -- ifetch cancels due to bypassing"}, */
{368, 368, "PME_ITA2_L2_IFET_CANCELS_CHG_PRIO", "L2_IFET_CANCELS_CHG_PRIO"},  /* { "L2_IFET_CANCELS_CHG_PRIO", {0xc00a1}, 0xf0, 1, {0x4020007}, "Instruction Fetch Cancels by the L2 -- ifetch cancels due to change priority"}, */
{369, 369, "PME_ITA2_L2_IFET_CANCELS_DATA_RD", "L2_IFET_CANCELS_DATA_RD"},  /* { "L2_IFET_CANCELS_DATA_RD", {0x700a1}, 0xf0, 1, {0x4020007}, "Instruction Fetch Cancels by the L2 -- ifetch/prefetch cancels due to a data read"}, */
{370, 370, "PME_ITA2_L2_IFET_CANCELS_DIDNT_RECIR", "L2_IFET_CANCELS_DIDNT_RECIR"},  /* { "L2_IFET_CANCELS_DIDNT_RECIR", {0x400a1}, 0xf0, 1, {0x4020007}, "Instruction Fetch Cancels by the L2 -- ifetch cancels because it did not recirculate"}, */
{371, 371, "PME_ITA2_L2_IFET_CANCELS_IFETCH_BYP", "L2_IFET_CANCELS_IFETCH_BYP"},  /* { "L2_IFET_CANCELS_IFETCH_BYP", {0xd00a1}, 0xf0, 1, {0x4020007}, "Instruction Fetch Cancels by the L2 -- due to ifetch bypass during last clock"}, */
{372, 372, "PME_ITA2_L2_IFET_CANCELS_PREEMPT", "L2_IFET_CANCELS_PREEMPT"},  /* { "L2_IFET_CANCELS_PREEMPT", {0x800a1}, 0xf0, 1, {0x4020007}, "Instruction Fetch Cancels by the L2 -- ifetch cancels due to preempts"}, */
{373, 373, "PME_ITA2_L2_IFET_CANCELS_RECIR_OVER_SUB", "L2_IFET_CANCELS_RECIR_OVER_SUB"},  /* { "L2_IFET_CANCELS_RECIR_OVER_SUB", {0x500a1}, 0xf0, 1, {0x4020007}, "Instruction Fetch Cancels by the L2 -- ifetch cancels because of recirculate oversubscription"}, */
{374, 374, "PME_ITA2_L2_IFET_CANCELS_ST_FILL_WB", "L2_IFET_CANCELS_ST_FILL_WB"},  /* { "L2_IFET_CANCELS_ST_FILL_WB", {0x600a1}, 0xf0, 1, {0x4020007}, "Instruction Fetch Cancels by the L2 -- ifetch cancels due to a store or fill or write back"}, */
{375, 375, "PME_ITA2_L2_INST_DEMAND_READS", "L2_INST_DEMAND_READS"},  /* { "L2_INST_DEMAND_READS", {0x42}, 0xf0, 1, {0xf00001}, "L2 Instruction Demand Fetch Requests"}, */
{376, 376, "PME_ITA2_L2_INST_PREFETCHES", "L2_INST_PREFETCHES"},  /* { "L2_INST_PREFETCHES", {0x45}, 0xf0, 1, {0xf00001}, "L2 Instruction Prefetch Requests"}, */
{377, 377, "PME_ITA2_L2_ISSUED_RECIRC_IFETCH_ANY", "L2_ISSUED_RECIRC_IFETCH_ANY"},  /* { "L2_ISSUED_RECIRC_IFETCH_ANY", {0x800b9}, 0xf0, 1, {0x4420007}, "Instruction Fetch Recirculates Issued by L2 -- Instruction fetch recirculates issued by L2"}, */
{378, 378, "PME_ITA2_L2_ISSUED_RECIRC_OZQ_ACC", "L2_ISSUED_RECIRC_OZQ_ACC"},  /* { "L2_ISSUED_RECIRC_OZQ_ACC", {0xb5}, 0xf0, 1, {0x4220007}, "Count Number of Times a Recirculate Issue Was Attempted and Not Preempted"}, */
{379, 379, "PME_ITA2_L2_L3ACCESS_CANCEL_ANY", "L2_L3ACCESS_CANCEL_ANY"},  /* { "L2_L3ACCESS_CANCEL_ANY", {0x900b0}, 0xf0, 1, {0x4120007}, "Canceled L3 Accesses -- count cancels due to any reason. This umask will count more than the sum of all the other umasks. It will count things that weren't committed accesses when they reached L1w, but the L2 attempted to bypass them to the L3 anyway (speculatively). This will include accesses made repeatedly while the main pipeline is stalled and the L1d is attempting to recirculate an access down the L1d pipeline. Thus, an access could get counted many times before it really does get bypassed to the L3. It is a measure of how many times we asserted a request to the L3 but didn't confirm it."}, */
{380, 380, "PME_ITA2_L2_L3ACCESS_CANCEL_DFETCH", "L2_L3ACCESS_CANCEL_DFETCH"},  /* { "L2_L3ACCESS_CANCEL_DFETCH", {0xa00b0}, 0xf0, 1, {0x4120007}, "Canceled L3 Accesses -- data fetches"}, */
{381, 381, "PME_ITA2_L2_L3ACCESS_CANCEL_EBL_REJECT", "L2_L3ACCESS_CANCEL_EBL_REJECT"},  /* { "L2_L3ACCESS_CANCEL_EBL_REJECT", {0x800b0}, 0xf0, 1, {0x4120007}, "Canceled L3 Accesses -- ebl rejects"}, */
{382, 382, "PME_ITA2_L2_L3ACCESS_CANCEL_FILLD_FULL", "L2_L3ACCESS_CANCEL_FILLD_FULL"},  /* { "L2_L3ACCESS_CANCEL_FILLD_FULL", {0x200b0}, 0xf0, 1, {0x4120007}, "Canceled L3 Accesses -- filld being full"}, */
{383, 383, "PME_ITA2_L2_L3ACCESS_CANCEL_IFETCH", "L2_L3ACCESS_CANCEL_IFETCH"},  /* { "L2_L3ACCESS_CANCEL_IFETCH", {0xb00b0}, 0xf0, 1, {0x4120007}, "Canceled L3 Accesses -- instruction fetches"}, */
{384, 384, "PME_ITA2_L2_L3ACCESS_CANCEL_INV_L3_BYP", "L2_L3ACCESS_CANCEL_INV_L3_BYP"},  /* { "L2_L3ACCESS_CANCEL_INV_L3_BYP", {0x600b0}, 0xf0, 1, {0x4120007}, "Canceled L3 Accesses -- invalid L3 bypasses"}, */
{385, 385, "PME_ITA2_L2_L3ACCESS_CANCEL_SPEC_L3_BYP", "L2_L3ACCESS_CANCEL_SPEC_L3_BYP"},  /* { "L2_L3ACCESS_CANCEL_SPEC_L3_BYP", {0x100b0}, 0xf0, 1, {0x4120007}, "Canceled L3 Accesses -- speculative L3 bypasses"}, */
{386, 386, "PME_ITA2_L2_L3ACCESS_CANCEL_UC_BLOCKED", "L2_L3ACCESS_CANCEL_UC_BLOCKED"},  /* { "L2_L3ACCESS_CANCEL_UC_BLOCKED", {0x500b0}, 0xf0, 1, {0x4120007}, "Canceled L3 Accesses -- Uncacheable blocked L3 Accesses"}, */
{387, 387, "PME_ITA2_L2_MISSES", "L2_MISSES"},  /* { "L2_MISSES", {0xcb}, 0xf0, 1, {0xf00007}, "L2 Misses"}, */
{388, 388, "PME_ITA2_L2_OPS_ISSUED_FP_LOAD", "L2_OPS_ISSUED_FP_LOAD"},  /* { "L2_OPS_ISSUED_FP_LOAD", {0x900b8}, 0xf0, 4, {0x4420007}, "Different Operations Issued by L2D -- Count only valid floating point loads"}, */
{389, 389, "PME_ITA2_L2_OPS_ISSUED_INT_LOAD", "L2_OPS_ISSUED_INT_LOAD"},  /* { "L2_OPS_ISSUED_INT_LOAD", {0x800b8}, 0xf0, 4, {0x4420007}, "Different Operations Issued by L2D -- Count only valid integer loads"}, */
{390, 390, "PME_ITA2_L2_OPS_ISSUED_NST_NLD", "L2_OPS_ISSUED_NST_NLD"},  /* { "L2_OPS_ISSUED_NST_NLD", {0xc00b8}, 0xf0, 4, {0x4420007}, "Different Operations Issued by L2D -- Count only valid non-load, no-store accesses"}, */
{391, 391, "PME_ITA2_L2_OPS_ISSUED_RMW", "L2_OPS_ISSUED_RMW"},  /* { "L2_OPS_ISSUED_RMW", {0xa00b8}, 0xf0, 4, {0x4420007}, "Different Operations Issued by L2D -- Count only valid read_modify_write stores"}, */
{392, 392, "PME_ITA2_L2_OPS_ISSUED_STORE", "L2_OPS_ISSUED_STORE"},  /* { "L2_OPS_ISSUED_STORE", {0xb00b8}, 0xf0, 4, {0x4420007}, "Different Operations Issued by L2D -- Count only valid non-read_modify_write stores"}, */
{393, 393, "PME_ITA2_L2_OZDB_FULL_THIS", "L2_OZDB_FULL_THIS"},  /* { "L2_OZDB_FULL_THIS", {0xbd}, 0xf0, 1, {0x4520000}, "L2 OZ Data Buffer Is Full -- L2 OZ Data Buffer is full"}, */
{394, 394, "PME_ITA2_L2_OZQ_ACQUIRE", "L2_OZQ_ACQUIRE"},  /* { "L2_OZQ_ACQUIRE", {0xa2}, 0xf0, 1, {0x4020000}, "Clocks With Acquire Ordering Attribute Existed in L2 OZQ"}, */
{395, 395, "PME_ITA2_L2_OZQ_CANCELS0_ANY", "L2_OZQ_CANCELS0_ANY"},  /* { "L2_OZQ_CANCELS0_ANY", {0xa0}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Late or Any) -- counts the total OZ Queue cancels"}, */
{396, 396, "PME_ITA2_L2_OZQ_CANCELS0_LATE_ACQUIRE", "L2_OZQ_CANCELS0_LATE_ACQUIRE"},  /* { "L2_OZQ_CANCELS0_LATE_ACQUIRE", {0x300a0}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Late or Any) -- counts the late cancels caused by acquires"}, */
{397, 397, "PME_ITA2_L2_OZQ_CANCELS0_LATE_BYP_EFFRELEASE", "L2_OZQ_CANCELS0_LATE_BYP_EFFRELEASE"},  /* { "L2_OZQ_CANCELS0_LATE_BYP_EFFRELEASE", {0x400a0}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Late or Any) -- counts the late cancels caused by L1D to L2A bypass effective releases"}, */
{398, 398, "PME_ITA2_L2_OZQ_CANCELS0_LATE_RELEASE", "L2_OZQ_CANCELS0_LATE_RELEASE"},  /* { "L2_OZQ_CANCELS0_LATE_RELEASE", {0x200a0}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Late or Any) -- counts the late cancels caused by releases"}, */
{399, 399, "PME_ITA2_L2_OZQ_CANCELS0_LATE_SPEC_BYP", "L2_OZQ_CANCELS0_LATE_SPEC_BYP"},  /* { "L2_OZQ_CANCELS0_LATE_SPEC_BYP", {0x100a0}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Late or Any) -- counts the late cancels caused by speculative bypasses"}, */
{400, 400, "PME_ITA2_L2_OZQ_CANCELS1_BANK_CONF", "L2_OZQ_CANCELS1_BANK_CONF"},  /* { "L2_OZQ_CANCELS1_BANK_CONF", {0x100ac}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 1) -- bank conflicts"}, */
{401, 401, "PME_ITA2_L2_OZQ_CANCELS1_CANC_L2M_ST", "L2_OZQ_CANCELS1_CANC_L2M_ST"},  /* { "L2_OZQ_CANCELS1_CANC_L2M_ST", {0x600ac}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 1) -- caused by a canceled store in L2M"}, */
{402, 402, "PME_ITA2_L2_OZQ_CANCELS1_CCV", "L2_OZQ_CANCELS1_CCV"},  /* { "L2_OZQ_CANCELS1_CCV", {0x900ac}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 1) -- a ccv"}, */
{403, 403, "PME_ITA2_L2_OZQ_CANCELS1_ECC", "L2_OZQ_CANCELS1_ECC"},  /* { "L2_OZQ_CANCELS1_ECC", {0xf00ac}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 1) -- ECC hardware detecting a problem"}, */
{404, 404, "PME_ITA2_L2_OZQ_CANCELS1_HPW_IFETCH_CONF", "L2_OZQ_CANCELS1_HPW_IFETCH_CONF"},  /* { "L2_OZQ_CANCELS1_HPW_IFETCH_CONF", {0x500ac}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 1) -- a ifetch conflict (canceling HPW?)"}, */
{405, 405, "PME_ITA2_L2_OZQ_CANCELS1_L1DF_L2M", "L2_OZQ_CANCELS1_L1DF_L2M"},  /* { "L2_OZQ_CANCELS1_L1DF_L2M", {0xe00ac}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 1) -- L1D fill in L2M"}, */
{406, 406, "PME_ITA2_L2_OZQ_CANCELS1_L1_FILL_CONF", "L2_OZQ_CANCELS1_L1_FILL_CONF"},  /* { "L2_OZQ_CANCELS1_L1_FILL_CONF", {0x700ac}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 1) -- an L1 fill conflict"}, */
{407, 407, "PME_ITA2_L2_OZQ_CANCELS1_L2A_ST_MAT", "L2_OZQ_CANCELS1_L2A_ST_MAT"},  /* { "L2_OZQ_CANCELS1_L2A_ST_MAT", {0xd00ac}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 1) -- a store match in L2A"}, */
{408, 408, "PME_ITA2_L2_OZQ_CANCELS1_L2D_ST_MAT", "L2_OZQ_CANCELS1_L2D_ST_MAT"},  /* { "L2_OZQ_CANCELS1_L2D_ST_MAT", {0x200ac}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 1) -- a store match in L2D"}, */
{409, 409, "PME_ITA2_L2_OZQ_CANCELS1_L2M_ST_MAT", "L2_OZQ_CANCELS1_L2M_ST_MAT"},  /* { "L2_OZQ_CANCELS1_L2M_ST_MAT", {0xb00ac}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 1) -- a store match in L2M"}, */
{410, 410, "PME_ITA2_L2_OZQ_CANCELS1_MFA", "L2_OZQ_CANCELS1_MFA"},  /* { "L2_OZQ_CANCELS1_MFA", {0xc00ac}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 1) -- a memory fence instruction"}, */
{411, 411, "PME_ITA2_L2_OZQ_CANCELS1_REL", "L2_OZQ_CANCELS1_REL"},  /* { "L2_OZQ_CANCELS1_REL", {0xac}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 1) -- caused by release"}, */
{412, 412, "PME_ITA2_L2_OZQ_CANCELS1_SEM", "L2_OZQ_CANCELS1_SEM"},  /* { "L2_OZQ_CANCELS1_SEM", {0xa00ac}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 1) -- a semaphore"}, */
{413, 413, "PME_ITA2_L2_OZQ_CANCELS1_ST_FILL_CONF", "L2_OZQ_CANCELS1_ST_FILL_CONF"},  /* { "L2_OZQ_CANCELS1_ST_FILL_CONF", {0x800ac}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 1) -- a store fill conflict"}, */
{414, 414, "PME_ITA2_L2_OZQ_CANCELS1_SYNC", "L2_OZQ_CANCELS1_SYNC"},  /* { "L2_OZQ_CANCELS1_SYNC", {0x400ac}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 1) -- caused by sync.i"}, */
{415, 415, "PME_ITA2_L2_OZQ_CANCELS2_ACQ", "L2_OZQ_CANCELS2_ACQ"},  /* { "L2_OZQ_CANCELS2_ACQ", {0x400a8}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 2) -- caused by an acquire"}, */
{416, 416, "PME_ITA2_L2_OZQ_CANCELS2_CANC_L2C_ST", "L2_OZQ_CANCELS2_CANC_L2C_ST"},  /* { "L2_OZQ_CANCELS2_CANC_L2C_ST", {0x100a8}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 2) -- caused by a canceled store in L2C"}, */
{417, 417, "PME_ITA2_L2_OZQ_CANCELS2_CANC_L2D_ST", "L2_OZQ_CANCELS2_CANC_L2D_ST"},  /* { "L2_OZQ_CANCELS2_CANC_L2D_ST", {0xd00a8}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 2) -- caused by a canceled store in L2D"}, */
{418, 418, "PME_ITA2_L2_OZQ_CANCELS2_DIDNT_RECIRC", "L2_OZQ_CANCELS2_DIDNT_RECIRC"},  /* { "L2_OZQ_CANCELS2_DIDNT_RECIRC", {0x900a8}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 2) -- caused because it did not recirculate"}, */
{419, 419, "PME_ITA2_L2_OZQ_CANCELS2_D_IFET", "L2_OZQ_CANCELS2_D_IFET"},  /* { "L2_OZQ_CANCELS2_D_IFET", {0xf00a8}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 2) -- a demand ifetch"}, */
{420, 420, "PME_ITA2_L2_OZQ_CANCELS2_L2C_ST_MAT", "L2_OZQ_CANCELS2_L2C_ST_MAT"},  /* { "L2_OZQ_CANCELS2_L2C_ST_MAT", {0x200a8}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 2) -- a store match in L2C"}, */
{421, 421, "PME_ITA2_L2_OZQ_CANCELS2_L2FILL_ST_CONF", "L2_OZQ_CANCELS2_L2FILL_ST_CONF"},  /* { "L2_OZQ_CANCELS2_L2FILL_ST_CONF", {0x800a8}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 2) -- a L2fill and store conflict in L2C"}, */
{422, 422, "PME_ITA2_L2_OZQ_CANCELS2_OVER_SUB", "L2_OZQ_CANCELS2_OVER_SUB"},  /* { "L2_OZQ_CANCELS2_OVER_SUB", {0xc00a8}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 2) -- oversubscription"}, */
{423, 423, "PME_ITA2_L2_OZQ_CANCELS2_OZ_DATA_CONF", "L2_OZQ_CANCELS2_OZ_DATA_CONF"},  /* { "L2_OZQ_CANCELS2_OZ_DATA_CONF", {0x600a8}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 2) -- an OZ data conflict"}, */
{424, 424, "PME_ITA2_L2_OZQ_CANCELS2_READ_WB_CONF", "L2_OZQ_CANCELS2_READ_WB_CONF"},  /* { "L2_OZQ_CANCELS2_READ_WB_CONF", {0x500a8}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 2) -- a write back conflict (canceling read?)"}, */
{425, 425, "PME_ITA2_L2_OZQ_CANCELS2_RECIRC_OVER_SUB", "L2_OZQ_CANCELS2_RECIRC_OVER_SUB"},  /* { "L2_OZQ_CANCELS2_RECIRC_OVER_SUB", {0xa8}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 2) -- caused by a recirculate oversubscription"}, */
{426, 426, "PME_ITA2_L2_OZQ_CANCELS2_SCRUB", "L2_OZQ_CANCELS2_SCRUB"},  /* { "L2_OZQ_CANCELS2_SCRUB", {0x300a8}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 2) -- 32/64 byte HPW/L2D fill which needs scrub"}, */
{427, 427, "PME_ITA2_L2_OZQ_CANCELS2_WEIRD", "L2_OZQ_CANCELS2_WEIRD"},  /* { "L2_OZQ_CANCELS2_WEIRD", {0xa00a8}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 2) -- counts the cancels caused by attempted 5-cycle bypasses for non-aligned accesses and bypasses blocking recirculates for too long"}, */
{428, 428, "PME_ITA2_L2_OZQ_FULL_THIS", "L2_OZQ_FULL_THIS"},  /* { "L2_OZQ_FULL_THIS", {0xbc}, 0xf0, 1, {0x4520000}, "L2D OZQ Is Full -- L2D OZQ is full"}, */
{429, 429, "PME_ITA2_L2_OZQ_RELEASE", "L2_OZQ_RELEASE"},  /* { "L2_OZQ_RELEASE", {0xa3}, 0xf0, 1, {0x4020000}, "Clocks With Release Ordering Attribute Existed in L2 OZQ"}, */
{430, 430, "PME_ITA2_L2_REFERENCES", "L2_REFERENCES"},  /* { "L2_REFERENCES", {0xb1}, 0xf0, 4, {0x4120007}, "Requests Made To L2"}, */
{431, 431, "PME_ITA2_L2_STORE_HIT_SHARED_ANY", "L2_STORE_HIT_SHARED_ANY"},  /* { "L2_STORE_HIT_SHARED_ANY", {0xba}, 0xf0, 2, {0x4320007}, "Store Hit a Shared Line -- Store hit a shared line"}, */
{432, 432, "PME_ITA2_L2_SYNTH_PROBE", "L2_SYNTH_PROBE"},  /* { "L2_SYNTH_PROBE", {0xb7}, 0xf0, 1, {0x4220007}, "Synthesized Probe"}, */
{433, 433, "PME_ITA2_L2_VICTIMB_FULL_THIS", "L2_VICTIMB_FULL_THIS"},  /* { "L2_VICTIMB_FULL_THIS", {0xbe}, 0xf0, 1, {0x4520000}, "L2D Victim Buffer Is Full -- L2D victim buffer is full"}, */
{434, 434, "PME_ITA2_L3_LINES_REPLACED", "L3_LINES_REPLACED"},  /* { "L3_LINES_REPLACED", {0xdf}, 0xf0, 1, {0xf00000}, "L3 Cache Lines Replaced"}, */
{435, 435, "PME_ITA2_L3_MISSES", "L3_MISSES"},  /* { "L3_MISSES", {0xdc}, 0xf0, 1, {0xf00007}, "L3 Misses"}, */
{436, 436, "PME_ITA2_L3_READS_ALL_ALL", "L3_READS_ALL_ALL"},  /* { "L3_READS_ALL_ALL", {0xf00dd}, 0xf0, 1, {0xf00007}, "L3 Reads -- L3 Read References"}, */
{437, 437, "PME_ITA2_L3_READS_ALL_HIT", "L3_READS_ALL_HIT"},  /* { "L3_READS_ALL_HIT", {0xd00dd}, 0xf0, 1, {0xf00007}, "L3 Reads -- L3 Read Hits"}, */
{438, 438, "PME_ITA2_L3_READS_ALL_MISS", "L3_READS_ALL_MISS"},  /* { "L3_READS_ALL_MISS", {0xe00dd}, 0xf0, 1, {0xf00007}, "L3 Reads -- L3 Read Misses"}, */
{439, 439, "PME_ITA2_L3_READS_DATA_READ_ALL", "L3_READS_DATA_READ_ALL"},  /* { "L3_READS_DATA_READ_ALL", {0xb00dd}, 0xf0, 1, {0xf00007}, "L3 Reads -- L3 Load References (excludes reads for ownership used to satisfy stores)"}, */
{440, 440, "PME_ITA2_L3_READS_DATA_READ_HIT", "L3_READS_DATA_READ_HIT"},  /* { "L3_READS_DATA_READ_HIT", {0x900dd}, 0xf0, 1, {0xf00007}, "L3 Reads -- L3 Load Hits (excludes reads for ownership used to satisfy stores)"}, */
{441, 441, "PME_ITA2_L3_READS_DATA_READ_MISS", "L3_READS_DATA_READ_MISS"},  /* { "L3_READS_DATA_READ_MISS", {0xa00dd}, 0xf0, 1, {0xf00007}, "L3 Reads -- L3 Load Misses (excludes reads for ownership used to satisfy stores)"}, */
{442, 442, "PME_ITA2_L3_READS_DINST_FETCH_ALL", "L3_READS_DINST_FETCH_ALL"},  /* { "L3_READS_DINST_FETCH_ALL", {0x300dd}, 0xf0, 1, {0xf00007}, "L3 Reads -- L3 Demand Instruction References"}, */
{443, 443, "PME_ITA2_L3_READS_DINST_FETCH_HIT", "L3_READS_DINST_FETCH_HIT"},  /* { "L3_READS_DINST_FETCH_HIT", {0x100dd}, 0xf0, 1, {0xf00007}, "L3 Reads -- L3 Demand Instruction Fetch Hits"}, */
{444, 444, "PME_ITA2_L3_READS_DINST_FETCH_MISS", "L3_READS_DINST_FETCH_MISS"},  /* { "L3_READS_DINST_FETCH_MISS", {0x200dd}, 0xf0, 1, {0xf00007}, "L3 Reads -- L3 Demand Instruction Fetch Misses"}, */
{445, 445, "PME_ITA2_L3_READS_INST_FETCH_ALL", "L3_READS_INST_FETCH_ALL"},  /* { "L3_READS_INST_FETCH_ALL", {0x700dd}, 0xf0, 1, {0xf00007}, "L3 Reads -- L3 Instruction Fetch and Prefetch References"}, */
{446, 446, "PME_ITA2_L3_READS_INST_FETCH_HIT", "L3_READS_INST_FETCH_HIT"},  /* { "L3_READS_INST_FETCH_HIT", {0x500dd}, 0xf0, 1, {0xf00007}, "L3 Reads -- L3 Instruction Fetch and Prefetch Hits"}, */
{447, 447, "PME_ITA2_L3_READS_INST_FETCH_MISS", "L3_READS_INST_FETCH_MISS"},  /* { "L3_READS_INST_FETCH_MISS", {0x600dd}, 0xf0, 1, {0xf00007}, "L3 Reads -- L3 Instruction Fetch and Prefetch Misses"}, */
{448, 448, "PME_ITA2_L3_REFERENCES", "L3_REFERENCES"},  /* { "L3_REFERENCES", {0xdb}, 0xf0, 1, {0xf00007}, "L3 References"}, */
{449, 449, "PME_ITA2_L3_WRITES_ALL_ALL", "L3_WRITES_ALL_ALL"},  /* { "L3_WRITES_ALL_ALL", {0xf00de}, 0xf0, 1, {0xf00007}, "L3 Writes -- L3 Write References"}, */
{450, 450, "PME_ITA2_L3_WRITES_ALL_HIT", "L3_WRITES_ALL_HIT"},  /* { "L3_WRITES_ALL_HIT", {0xd00de}, 0xf0, 1, {0xf00007}, "L3 Writes -- L3 Write Hits"}, */
{451, 451, "PME_ITA2_L3_WRITES_ALL_MISS", "L3_WRITES_ALL_MISS"},  /* { "L3_WRITES_ALL_MISS", {0xe00de}, 0xf0, 1, {0xf00007}, "L3 Writes -- L3 Write Misses"}, */
{452, 452, "PME_ITA2_L3_WRITES_DATA_WRITE_ALL", "L3_WRITES_DATA_WRITE_ALL"},  /* { "L3_WRITES_DATA_WRITE_ALL", {0x700de}, 0xf0, 1, {0xf00007}, "L3 Writes -- L3 Store References (excludes L2 write backs, includes L3 read for ownership requests that satisfy stores)"}, */
{453, 453, "PME_ITA2_L3_WRITES_DATA_WRITE_HIT", "L3_WRITES_DATA_WRITE_HIT"},  /* { "L3_WRITES_DATA_WRITE_HIT", {0x500de}, 0xf0, 1, {0xf00007}, "L3 Writes -- L3 Store Hits (excludes L2 write backs, includes L3 read for ownership requests that satisfy stores)"}, */
{454, 454, "PME_ITA2_L3_WRITES_DATA_WRITE_MISS", "L3_WRITES_DATA_WRITE_MISS"},  /* { "L3_WRITES_DATA_WRITE_MISS", {0x600de}, 0xf0, 1, {0xf00007}, "L3 Writes -- L3 Store Misses (excludes L2 write backs, includes L3 read for ownership requests that satisfy stores)"}, */
{455, 455, "PME_ITA2_L3_WRITES_L2_WB_ALL", "L3_WRITES_L2_WB_ALL"},  /* { "L3_WRITES_L2_WB_ALL", {0xb00de}, 0xf0, 1, {0xf00007}, "L3 Writes -- L2 Write Back References"}, */
{456, 456, "PME_ITA2_L3_WRITES_L2_WB_HIT", "L3_WRITES_L2_WB_HIT"},  /* { "L3_WRITES_L2_WB_HIT", {0x900de}, 0xf0, 1, {0xf00007}, "L3 Writes -- L2 Write Back Hits"}, */
{457, 457, "PME_ITA2_L3_WRITES_L2_WB_MISS", "L3_WRITES_L2_WB_MISS"},  /* { "L3_WRITES_L2_WB_MISS", {0xa00de}, 0xf0, 1, {0xf00007}, "L3 Writes -- L2 Write Back Misses"}, */
{458, 458, "PME_ITA2_LOADS_RETIRED", "LOADS_RETIRED"},  /* { "LOADS_RETIRED", {0xcd}, 0xf0, 4, {0x5310007}, "Retired Loads"}, */
{459, 459, "PME_ITA2_MEM_READ_CURRENT_ANY", "MEM_READ_CURRENT_ANY"},  /* { "MEM_READ_CURRENT_ANY", {0x30089}, 0xf0, 1, {0xf00000}, "Current Mem Read Transactions On Bus -- CPU or non-CPU (all transactions)."}, */
{460, 460, "PME_ITA2_MEM_READ_CURRENT_IO", "MEM_READ_CURRENT_IO"},  /* { "MEM_READ_CURRENT_IO", {0x10089}, 0xf0, 1, {0xf00000}, "Current Mem Read Transactions On Bus -- non-CPU priority agents"}, */
{461, 461, "PME_ITA2_MISALIGNED_LOADS_RETIRED", "MISALIGNED_LOADS_RETIRED"},  /* { "MISALIGNED_LOADS_RETIRED", {0xce}, 0xf0, 4, {0x5310007}, "Retired Misaligned Load Instructions"}, */
{462, 462, "PME_ITA2_MISALIGNED_STORES_RETIRED", "MISALIGNED_STORES_RETIRED"},  /* { "MISALIGNED_STORES_RETIRED", {0xd2}, 0xf0, 2, {0x5410007}, "Retired Misaligned Store Instructions"}, */
{463, 463, "PME_ITA2_NOPS_RETIRED", "NOPS_RETIRED"},  /* { "NOPS_RETIRED", {0x50}, 0xf0, 6, {0xf00003}, "Retired NOP Instructions"}, */
{464, 464, "PME_ITA2_PREDICATE_SQUASHED_RETIRED", "PREDICATE_SQUASHED_RETIRED"},  /* { "PREDICATE_SQUASHED_RETIRED", {0x51}, 0xf0, 6, {0xf00003}, "Instructions Squashed Due to Predicate Off"}, */
{465, 465, "PME_ITA2_RSE_CURRENT_REGS_2_TO_0", "RSE_CURRENT_REGS_2_TO_0"},  /* { "RSE_CURRENT_REGS_2_TO_0", {0x2b}, 0xf0, 7, {0xf00000}, "Current RSE Registers (Bits 2:0)"}, */
{466, 466, "PME_ITA2_RSE_CURRENT_REGS_5_TO_3", "RSE_CURRENT_REGS_5_TO_3"},  /* { "RSE_CURRENT_REGS_5_TO_3", {0x2a}, 0xf0, 7, {0xf00000}, "Current RSE Registers (Bits 5:3)"}, */
{467, 467, "PME_ITA2_RSE_CURRENT_REGS_6", "RSE_CURRENT_REGS_6"},  /* { "RSE_CURRENT_REGS_6", {0x26}, 0xf0, 1, {0xf00000}, "Current RSE Registers (Bit 6)"}, */
{468, 468, "PME_ITA2_RSE_DIRTY_REGS_2_TO_0", "RSE_DIRTY_REGS_2_TO_0"},  /* { "RSE_DIRTY_REGS_2_TO_0", {0x29}, 0xf0, 7, {0xf00000}, "Dirty RSE Registers (Bits 2:0)"}, */
{469, 469, "PME_ITA2_RSE_DIRTY_REGS_5_TO_3", "RSE_DIRTY_REGS_5_TO_3"},  /* { "RSE_DIRTY_REGS_5_TO_3", {0x28}, 0xf0, 7, {0xf00000}, "Dirty RSE Registers (Bits 5:3)"}, */
{470, 470, "PME_ITA2_RSE_DIRTY_REGS_6", "RSE_DIRTY_REGS_6"},  /* { "RSE_DIRTY_REGS_6", {0x24}, 0xf0, 1, {0xf00000}, "Dirty RSE Registers (Bit 6)"}, */
{471, 471, "PME_ITA2_RSE_EVENT_RETIRED", "RSE_EVENT_RETIRED"},  /* { "RSE_EVENT_RETIRED", {0x32}, 0xf0, 1, {0xf00000}, "Retired RSE operations"}, */
{472, 472, "PME_ITA2_RSE_REFERENCES_RETIRED_ALL", "RSE_REFERENCES_RETIRED_ALL"},  /* { "RSE_REFERENCES_RETIRED_ALL", {0x30020}, 0xf0, 2, {0xf00007}, "RSE Accesses -- Both RSE loads and stores will be counted."}, */
{473, 473, "PME_ITA2_RSE_REFERENCES_RETIRED_LOAD", "RSE_REFERENCES_RETIRED_LOAD"},  /* { "RSE_REFERENCES_RETIRED_LOAD", {0x10020}, 0xf0, 2, {0xf00007}, "RSE Accesses -- Only RSE loads will be counted."}, */
{474, 474, "PME_ITA2_RSE_REFERENCES_RETIRED_STORE", "RSE_REFERENCES_RETIRED_STORE"},  /* { "RSE_REFERENCES_RETIRED_STORE", {0x20020}, 0xf0, 2, {0xf00007}, "RSE Accesses -- Only RSE stores will be counted."}, */
{475, 475, "PME_ITA2_SERIALIZATION_EVENTS", "SERIALIZATION_EVENTS"},  /* { "SERIALIZATION_EVENTS", {0x53}, 0xf0, 1, {0xf00000}, "Number of srlz.i Instructions"}, */
{476, 476, "PME_ITA2_STORES_RETIRED", "STORES_RETIRED"},  /* { "STORES_RETIRED", {0xd1}, 0xf0, 2, {0x5410007}, "Retired Stores"}, */
{477, 477, "PME_ITA2_SYLL_NOT_DISPERSED_ALL", "SYLL_NOT_DISPERSED_ALL"},  /* { "SYLL_NOT_DISPERSED_ALL", {0xf004e}, 0xf0, 5, {0xf00001}, "Syllables Not Dispersed -- Counts all syllables not dispersed. NOTE: Any combination of b0000-b1111 is valid."}, */
{478, 478, "PME_ITA2_SYLL_NOT_DISPERSED_EXPL", "SYLL_NOT_DISPERSED_EXPL"},  /* { "SYLL_NOT_DISPERSED_EXPL", {0x1004e}, 0xf0, 5, {0xf00001}, "Syllables Not Dispersed -- Count syllables not dispersed due to explicit stop bits. These consist of  programmer specified architected S-bit and templates 1 and 5. Dispersal takes a 6-syllable (3-syllable) hit for every template 1/5 in bundle 0(1). Dispersal takes a 3-syllable (0 syllable) hit for every S-bit in bundle 0(1)"}, */
{479, 479, "PME_ITA2_SYLL_NOT_DISPERSED_EXPL_OR_FE", "SYLL_NOT_DISPERSED_EXPL_OR_FE"},  /* { "SYLL_NOT_DISPERSED_EXPL_OR_FE", {0x5004e}, 0xf0, 5, {0xf00001}, "Syllables Not Dispersed -- Count syllables not dispersed due to explicit stop bits or front-end not providing valid bundles or providing valid illegal templates."}, */
{480, 480, "PME_ITA2_SYLL_NOT_DISPERSED_EXPL_OR_FE_OR_MLI", "SYLL_NOT_DISPERSED_EXPL_OR_FE_OR_MLI"},  /* { "SYLL_NOT_DISPERSED_EXPL_OR_FE_OR_MLI", {0xd004e}, 0xf0, 5, {0xf00001}, "Syllables Not Dispersed -- Count syllables not dispersed due to explicit stop bits or due to front-end not providing valid bundles or providing valid illegal templates or due to MLI bundle and resteers to non-0 syllable."}, */
{481, 481, "PME_ITA2_SYLL_NOT_DISPERSED_EXPL_OR_IMPL", "SYLL_NOT_DISPERSED_EXPL_OR_IMPL"},  /* { "SYLL_NOT_DISPERSED_EXPL_OR_IMPL", {0x3004e}, 0xf0, 5, {0xf00001}, "Syllables Not Dispersed -- Count syllables not dispersed due to explicit/implicit stop bits."}, */
{482, 482, "PME_ITA2_SYLL_NOT_DISPERSED_EXPL_OR_IMPL_OR_FE", "SYLL_NOT_DISPERSED_EXPL_OR_IMPL_OR_FE"},  /* { "SYLL_NOT_DISPERSED_EXPL_OR_IMPL_OR_FE", {0x7004e}, 0xf0, 5, {0xf00001}, "Syllables Not Dispersed -- Count syllables not dispersed due to explicit or implicit stop bits or due to front-end not providing valid bundles or providing valid illegal template."}, */
{483, 483, "PME_ITA2_SYLL_NOT_DISPERSED_EXPL_OR_IMPL_OR_MLI", "SYLL_NOT_DISPERSED_EXPL_OR_IMPL_OR_MLI"},  /* { "SYLL_NOT_DISPERSED_EXPL_OR_IMPL_OR_MLI", {0xb004e}, 0xf0, 5, {0xf00001}, "Syllables Not Dispersed -- Count syllables not dispersed due to explicit or implicit stop bits or due to MLI bundle and resteers to non-0 syllable."}, */
{484, 484, "PME_ITA2_SYLL_NOT_DISPERSED_EXPL_OR_MLI", "SYLL_NOT_DISPERSED_EXPL_OR_MLI"},  /* { "SYLL_NOT_DISPERSED_EXPL_OR_MLI", {0x9004e}, 0xf0, 5, {0xf00001}, "Syllables Not Dispersed -- Count syllables not dispersed due to explicit stop bits or to MLI bundle and resteers to non-0 syllable."}, */
{485, 485, "PME_ITA2_SYLL_NOT_DISPERSED_FE", "SYLL_NOT_DISPERSED_FE"},  /* { "SYLL_NOT_DISPERSED_FE", {0x4004e}, 0xf0, 5, {0xf00001}, "Syllables Not Dispersed -- Count syllables not dispersed due to front-end not providing valid bundles or providing valid illegal templates. Dispersal takes a 3-syllable hit for every invalid bundle or valid illegal template from front-end. Bundle 1 with front-end fault, is counted here (3-syllable hit).."}, */
{486, 486, "PME_ITA2_SYLL_NOT_DISPERSED_FE_OR_MLI", "SYLL_NOT_DISPERSED_FE_OR_MLI"},  /* { "SYLL_NOT_DISPERSED_FE_OR_MLI", {0xc004e}, 0xf0, 5, {0xf00001}, "Syllables Not Dispersed -- Count syllables not dispersed due to MLI bundle and resteers to non-0 syllable or due to front-end not providing valid bundles or providing valid illegal templates."}, */
{487, 487, "PME_ITA2_SYLL_NOT_DISPERSED_IMPL", "SYLL_NOT_DISPERSED_IMPL"},  /* { "SYLL_NOT_DISPERSED_IMPL", {0x2004e}, 0xf0, 5, {0xf00001}, "Syllables Not Dispersed -- Count syllables not dispersed due to implicit stop bits. These consist of all of the non-architected stop bits (asymmetry, oversubscription, implicit). Dispersal takes a 6-syllable(3-syllable) hit for every implicit stop bits in bundle 0(1)."}, */
{488, 488, "PME_ITA2_SYLL_NOT_DISPERSED_IMPL_OR_FE", "SYLL_NOT_DISPERSED_IMPL_OR_FE"},  /* { "SYLL_NOT_DISPERSED_IMPL_OR_FE", {0x6004e}, 0xf0, 5, {0xf00001}, "Syllables Not Dispersed -- Count syllables not dispersed due to implicit stop bits or to front-end not providing valid bundles or providing valid illegal templates."}, */
{489, 489, "PME_ITA2_SYLL_NOT_DISPERSED_IMPL_OR_FE_OR_MLI", "SYLL_NOT_DISPERSED_IMPL_OR_FE_OR_MLI"},  /* { "SYLL_NOT_DISPERSED_IMPL_OR_FE_OR_MLI", {0xe004e}, 0xf0, 5, {0xf00001}, "Syllables Not Dispersed -- Count syllables not dispersed due to implicit stop bits or due to front-end not providing valid bundles or providing valid illegal templates or due to MLI bundle and resteers to non-0 syllable."}, */
{490, 490, "PME_ITA2_SYLL_NOT_DISPERSED_IMPL_OR_MLI", "SYLL_NOT_DISPERSED_IMPL_OR_MLI"},  /* { "SYLL_NOT_DISPERSED_IMPL_OR_MLI", {0xa004e}, 0xf0, 5, {0xf00001}, "Syllables Not Dispersed -- Count syllables not dispersed due to implicit stop bits or to MLI bundle and resteers to non-0 syllable."}, */
{491, 491, "PME_ITA2_SYLL_NOT_DISPERSED_MLI", "SYLL_NOT_DISPERSED_MLI"},  /* { "SYLL_NOT_DISPERSED_MLI", {0x8004e}, 0xf0, 5, {0xf00001}, "Syllables Not Dispersed -- Count syllables not dispersed due to MLI bundle and resteers to non-0 syllable. Dispersal takes a 1 syllable hit for each MLI bundle . Dispersal could take 0-2 syllable hit depending on which syllable we resteer to. Bundle 1 with front-end fault which is split, is counted here (0-2 syllable hit)."}, */
{492, 492, "PME_ITA2_SYLL_OVERCOUNT_ALL", "SYLL_OVERCOUNT_ALL"},  /* { "SYLL_OVERCOUNT_ALL", {0x3004f}, 0xf0, 2, {0xf00001}, "Syllables Overcounted -- syllables overcounted in implicit & explicit bucket"}, */
{493, 493, "PME_ITA2_SYLL_OVERCOUNT_EXPL", "SYLL_OVERCOUNT_EXPL"},  /* { "SYLL_OVERCOUNT_EXPL", {0x1004f}, 0xf0, 2, {0xf00001}, "Syllables Overcounted -- Only syllables overcounted in the explicit bucket"}, */
{494, 494, "PME_ITA2_SYLL_OVERCOUNT_IMPL", "SYLL_OVERCOUNT_IMPL"},  /* { "SYLL_OVERCOUNT_IMPL", {0x2004f}, 0xf0, 2, {0xf00001}, "Syllables Overcounted -- Only syllables overcounted in the implicit bucket"}, */
{495, 495, "PME_ITA2_UC_LOADS_RETIRED", "UC_LOADS_RETIRED"},  /* { "UC_LOADS_RETIRED", {0xcf}, 0xf0, 4, {0x5310007}, "Retired Uncacheable Loads"}, */
{496, 496, "PME_ITA2_UC_STORES_RETIRED", "UC_STORES_RETIRED"},  /* { "UC_STORES_RETIRED", {0xd0}, 0xf0, 2, {0x5410007}, "Retired Uncacheable Stores"}, */
{179, 179, str_nil, str_nil}
};

#else /* #if PFMLIB_MAJ_VERSION(PFMLIB_VERSION) > 2 */

  /* I1 (ia64) Itanium */

#define I1_NUMEVENTS 230
event_t I1_event[I1_NUMEVENTS + 1] = {
	{0, 0, str_nil, "ALAT_INST_CHKA_LDC_ALL"},	/* { "ALAT_INST_CHKA_LDC_ALL", {0x30036} , 0xf0, 2, {0xffff0003}, NULL}, */
	{1, 1, str_nil, "ALAT_INST_CHKA_LDC_FP"},	/* { "ALAT_INST_CHKA_LDC_FP", {0x10036} , 0xf0, 2, {0xffff0003}, NULL}, */
	{2, 2, str_nil, "ALAT_INST_CHKA_LDC_INT"},	/* { "ALAT_INST_CHKA_LDC_INT", {0x20036} , 0xf0, 2, {0xffff0003}, NULL}, */
	{3, 3, str_nil, "ALAT_INST_FAILED_CHKA_LDC_ALL"},	/* { "ALAT_INST_FAILED_CHKA_LDC_ALL", {0x30037} , 0xf0, 2, {0xffff0003}, NULL}, */
	{4, 4, str_nil, "ALAT_INST_FAILED_CHKA_LDC_FP"},	/* { "ALAT_INST_FAILED_CHKA_LDC_FP", {0x10037} , 0xf0, 2, {0xffff0003}, NULL}, */
	{5, 5, str_nil, "ALAT_INST_FAILED_CHKA_LDC_INT"},	/* { "ALAT_INST_FAILED_CHKA_LDC_INT", {0x20037} , 0xf0, 2, {0xffff0003}, NULL}, */
	{6, 6, str_nil, "ALAT_REPLACEMENT_ALL"},	/* { "ALAT_REPLACEMENT_ALL", {0x30038} , 0xf0, 2, {0xffff0007}, NULL}, */
	{7, 7, str_nil, "ALAT_REPLACEMENT_FP"},	/* { "ALAT_REPLACEMENT_FP", {0x10038} , 0xf0, 2, {0xffff0007}, NULL}, */
	{8, 8, str_nil, "ALAT_REPLACEMENT_INT"},	/* { "ALAT_REPLACEMENT_INT", {0x20038} , 0xf0, 2, {0xffff0007}, NULL}, */
	{9, 9, str_nil, "ALL_STOPS_DISPERSED"},	/* { "ALL_STOPS_DISPERSED", {0x2f} , 0xf0, 1, {0xffff0001}, NULL}, */
	{10, 10, str_nil, "BRANCH_EVENT"},	/* { "BRANCH_EVENT", {0x811} , 0xf0, 1, {0xffff0003}, NULL}, */
	{11, 11, str_nil, "BRANCH_MULTIWAY_ALL_PATHS_ALL_PREDICTIONS"},	/* { "BRANCH_MULTIWAY_ALL_PATHS_ALL_PREDICTIONS", {0xe} , 0xf0, 1, {0xffff0003}, NULL}, */
	{12, 12, str_nil, "BRANCH_MULTIWAY_ALL_PATHS_CORRECT_PREDICTIONS"},	/* { "BRANCH_MULTIWAY_ALL_PATHS_CORRECT_PREDICTIONS", {0x1000e} , 0xf0, 1, {0xffff0003}, NULL}, */
	{13, 13, str_nil, "BRANCH_MULTIWAY_ALL_PATHS_WRONG_PATH"},	/* { "BRANCH_MULTIWAY_ALL_PATHS_WRONG_PATH", {0x2000e} , 0xf0, 1, {0xffff0003}, NULL}, */
	{14, 14, str_nil, "BRANCH_MULTIWAY_ALL_PATHS_WRONG_TARGET"},	/* { "BRANCH_MULTIWAY_ALL_PATHS_WRONG_TARGET", {0x3000e} , 0xf0, 1, {0xffff0003}, NULL}, */
	{15, 15, str_nil, "BRANCH_MULTIWAY_NOT_TAKEN_ALL_PREDICTIONS"},	/* { "BRANCH_MULTIWAY_NOT_TAKEN_ALL_PREDICTIONS", {0x8000e} , 0xf0, 1, {0xffff0003}, NULL}, */
	{16, 16, str_nil, "BRANCH_MULTIWAY_NOT_TAKEN_CORRECT_PREDICTIONS"},	/* { "BRANCH_MULTIWAY_NOT_TAKEN_CORRECT_PREDICTIONS", {0x9000e} , 0xf0, 1, {0xffff0003}, NULL}, */
	{17, 17, str_nil, "BRANCH_MULTIWAY_NOT_TAKEN_WRONG_PATH"},	/* { "BRANCH_MULTIWAY_NOT_TAKEN_WRONG_PATH", {0xa000e} , 0xf0, 1, {0xffff0003}, NULL}, */
	{18, 18, str_nil, "BRANCH_MULTIWAY_NOT_TAKEN_WRONG_TARGET"},	/* { "BRANCH_MULTIWAY_NOT_TAKEN_WRONG_TARGET", {0xb000e} , 0xf0, 1, {0xffff0003}, NULL}, */
	{19, 19, str_nil, "BRANCH_MULTIWAY_TAKEN_ALL_PREDICTIONS"},	/* { "BRANCH_MULTIWAY_TAKEN_ALL_PREDICTIONS", {0xc000e} , 0xf0, 1, {0xffff0003}, NULL}, */
	{20, 20, str_nil, "BRANCH_MULTIWAY_TAKEN_CORRECT_PREDICTIONS"},	/* { "BRANCH_MULTIWAY_TAKEN_CORRECT_PREDICTIONS", {0xd000e} , 0xf0, 1, {0xffff0003}, NULL}, */
	{21, 21, str_nil, "BRANCH_MULTIWAY_TAKEN_WRONG_PATH"},	/* { "BRANCH_MULTIWAY_TAKEN_WRONG_PATH", {0xe000e} , 0xf0, 1, {0xffff0003}, NULL}, */
	{22, 22, str_nil, "BRANCH_MULTIWAY_TAKEN_WRONG_TARGET"},	/* { "BRANCH_MULTIWAY_TAKEN_WRONG_TARGET", {0xf000e} , 0xf0, 1, {0xffff0003}, NULL}, */
	{23, 23, str_nil, "BRANCH_NOT_TAKEN"},	/* { "BRANCH_NOT_TAKEN", {0x8000d} , 0xf0, 1, {0xffff0003}, NULL}, */
	{24, 24, str_nil, "BRANCH_PATH_1ST_STAGE_NT_OUTCOMES_CORRECTLY_PREDICTED"},	/* { "BRANCH_PATH_1ST_STAGE_NT_OUTCOMES_CORRECTLY_PREDICTED", {0x6000f} , 0xf0, 1, {0xffff0003}, NULL}, */
	{25, 25, str_nil, "BRANCH_PATH_1ST_STAGE_NT_OUTCOMES_INCORRECTLY_PREDICTED"},	/* { "BRANCH_PATH_1ST_STAGE_NT_OUTCOMES_INCORRECTLY_PREDICTED", {0x4000f} , 0xf0, 1, {0xffff0003}, NULL}, */
	{26, 26, str_nil, "BRANCH_PATH_1ST_STAGE_TK_OUTCOMES_CORRECTLY_PREDICTED"},	/* { "BRANCH_PATH_1ST_STAGE_TK_OUTCOMES_CORRECTLY_PREDICTED", {0x7000f} , 0xf0, 1, {0xffff0003}, NULL}, */
	{27, 27, str_nil, "BRANCH_PATH_1ST_STAGE_TK_OUTCOMES_INCORRECTLY_PREDICTED"},	/* { "BRANCH_PATH_1ST_STAGE_TK_OUTCOMES_INCORRECTLY_PREDICTED", {0x5000f} , 0xf0, 1, {0xffff0003}, NULL}, */
	{28, 28, str_nil, "BRANCH_PATH_2ND_STAGE_NT_OUTCOMES_CORRECTLY_PREDICTED"},	/* { "BRANCH_PATH_2ND_STAGE_NT_OUTCOMES_CORRECTLY_PREDICTED", {0xa000f} , 0xf0, 1, {0xffff0003}, NULL}, */
	{29, 29, str_nil, "BRANCH_PATH_2ND_STAGE_NT_OUTCOMES_INCORRECTLY_PREDICTED"},	/* { "BRANCH_PATH_2ND_STAGE_NT_OUTCOMES_INCORRECTLY_PREDICTED", {0x8000f} , 0xf0, 1, {0xffff0003}, NULL}, */
	{30, 30, str_nil, "BRANCH_PATH_2ND_STAGE_TK_OUTCOMES_CORRECTLY_PREDICTED"},	/* { "BRANCH_PATH_2ND_STAGE_TK_OUTCOMES_CORRECTLY_PREDICTED", {0xb000f} , 0xf0, 1, {0xffff0003}, NULL}, */
	{31, 31, str_nil, "BRANCH_PATH_2ND_STAGE_TK_OUTCOMES_INCORRECTLY_PREDICTED"},	/* { "BRANCH_PATH_2ND_STAGE_TK_OUTCOMES_INCORRECTLY_PREDICTED", {0x9000f} , 0xf0, 1, {0xffff0003}, NULL}, */
	{32, 32, str_nil, "BRANCH_PATH_3RD_STAGE_NT_OUTCOMES_CORRECTLY_PREDICTED"},	/* { "BRANCH_PATH_3RD_STAGE_NT_OUTCOMES_CORRECTLY_PREDICTED", {0xe000f} , 0xf0, 1, {0xffff0003}, NULL}, */
	{33, 33, str_nil, "BRANCH_PATH_3RD_STAGE_NT_OUTCOMES_INCORRECTLY_PREDICTED"},	/* { "BRANCH_PATH_3RD_STAGE_NT_OUTCOMES_INCORRECTLY_PREDICTED", {0xc000f} , 0xf0, 1, {0xffff0003}, NULL}, */
	{34, 34, str_nil, "BRANCH_PATH_3RD_STAGE_TK_OUTCOMES_CORRECTLY_PREDICTED"},	/* { "BRANCH_PATH_3RD_STAGE_TK_OUTCOMES_CORRECTLY_PREDICTED", {0xf000f} , 0xf0, 1, {0xffff0003}, NULL}, */
	{35, 35, str_nil, "BRANCH_PATH_3RD_STAGE_TK_OUTCOMES_INCORRECTLY_PREDICTED"},	/* { "BRANCH_PATH_3RD_STAGE_TK_OUTCOMES_INCORRECTLY_PREDICTED", {0xd000f} , 0xf0, 1, {0xffff0003}, NULL}, */
	{36, 36, str_nil, "BRANCH_PATH_ALL_NT_OUTCOMES_CORRECTLY_PREDICTED"},	/* { "BRANCH_PATH_ALL_NT_OUTCOMES_CORRECTLY_PREDICTED", {0x2000f} , 0xf0, 1, {0xffff0003}, NULL}, */
	{37, 37, str_nil, "BRANCH_PATH_ALL_NT_OUTCOMES_INCORRECTLY_PREDICTED"},	/* { "BRANCH_PATH_ALL_NT_OUTCOMES_INCORRECTLY_PREDICTED", {0xf} , 0xf0, 1, {0xffff0003}, NULL}, */
	{38, 38, str_nil, "BRANCH_PATH_ALL_TK_OUTCOMES_CORRECTLY_PREDICTED"},	/* { "BRANCH_PATH_ALL_TK_OUTCOMES_CORRECTLY_PREDICTED", {0x3000f} , 0xf0, 1, {0xffff0003}, NULL}, */
	{39, 39, str_nil, "BRANCH_PATH_ALL_TK_OUTCOMES_INCORRECTLY_PREDICTED"},	/* { "BRANCH_PATH_ALL_TK_OUTCOMES_INCORRECTLY_PREDICTED", {0x1000f} , 0xf0, 1, {0xffff0003}, NULL}, */
	{40, 40, str_nil, "BRANCH_PREDICTOR_1ST_STAGE_ALL_PREDICTIONS"},	/* { "BRANCH_PREDICTOR_1ST_STAGE_ALL_PREDICTIONS", {0x40010} , 0xf0, 1, {0xffff0003}, NULL}, */
	{41, 41, str_nil, "BRANCH_PREDICTOR_1ST_STAGE_CORRECT_PREDICTIONS"},	/* { "BRANCH_PREDICTOR_1ST_STAGE_CORRECT_PREDICTIONS", {0x50010} , 0xf0, 1, {0xffff0003}, NULL}, */
	{42, 42, str_nil, "BRANCH_PREDICTOR_1ST_STAGE_WRONG_PATH"},	/* { "BRANCH_PREDICTOR_1ST_STAGE_WRONG_PATH", {0x60010} , 0xf0, 1, {0xffff0003}, NULL}, */
	{43, 43, str_nil, "BRANCH_PREDICTOR_1ST_STAGE_WRONG_TARGET"},	/* { "BRANCH_PREDICTOR_1ST_STAGE_WRONG_TARGET", {0x70010} , 0xf0, 1, {0xffff0003}, NULL}, */
	{44, 44, str_nil, "BRANCH_PREDICTOR_2ND_STAGE_ALL_PREDICTIONS"},	/* { "BRANCH_PREDICTOR_2ND_STAGE_ALL_PREDICTIONS", {0x80010} , 0xf0, 1, {0xffff0003}, NULL}, */
	{45, 45, str_nil, "BRANCH_PREDICTOR_2ND_STAGE_CORRECT_PREDICTIONS"},	/* { "BRANCH_PREDICTOR_2ND_STAGE_CORRECT_PREDICTIONS", {0x90010} , 0xf0, 1, {0xffff0003}, NULL}, */
	{46, 46, str_nil, "BRANCH_PREDICTOR_2ND_STAGE_WRONG_PATH"},	/* { "BRANCH_PREDICTOR_2ND_STAGE_WRONG_PATH", {0xa0010} , 0xf0, 1, {0xffff0003}, NULL}, */
	{47, 47, str_nil, "BRANCH_PREDICTOR_2ND_STAGE_WRONG_TARGET"},	/* { "BRANCH_PREDICTOR_2ND_STAGE_WRONG_TARGET", {0xb0010} , 0xf0, 1, {0xffff0003}, NULL}, */
	{48, 48, str_nil, "BRANCH_PREDICTOR_3RD_STAGE_ALL_PREDICTIONS"},	/* { "BRANCH_PREDICTOR_3RD_STAGE_ALL_PREDICTIONS", {0xc0010} , 0xf0, 1, {0xffff0003}, NULL}, */
	{49, 49, str_nil, "BRANCH_PREDICTOR_3RD_STAGE_CORRECT_PREDICTIONS"},	/* { "BRANCH_PREDICTOR_3RD_STAGE_CORRECT_PREDICTIONS", {0xd0010} , 0xf0, 1, {0xffff0003}, NULL}, */
	{50, 50, str_nil, "BRANCH_PREDICTOR_3RD_STAGE_WRONG_PATH"},	/* { "BRANCH_PREDICTOR_3RD_STAGE_WRONG_PATH", {0xe0010} , 0xf0, 1, {0xffff0003}, NULL}, */
	{51, 51, str_nil, "BRANCH_PREDICTOR_3RD_STAGE_WRONG_TARGET"},	/* { "BRANCH_PREDICTOR_3RD_STAGE_WRONG_TARGET", {0xf0010} , 0xf0, 1, {0xffff0003}, NULL}, */
	{52, 52, str_nil, "BRANCH_PREDICTOR_ALL_ALL_PREDICTIONS"},	/* { "BRANCH_PREDICTOR_ALL_ALL_PREDICTIONS", {0x10} , 0xf0, 1, {0xffff0003}, NULL}, */
	{53, 53, str_nil, "BRANCH_PREDICTOR_ALL_CORRECT_PREDICTIONS"},	/* { "BRANCH_PREDICTOR_ALL_CORRECT_PREDICTIONS", {0x10010} , 0xf0, 1, {0xffff0003}, NULL}, */
	{54, 54, str_nil, "BRANCH_PREDICTOR_ALL_WRONG_PATH"},	/* { "BRANCH_PREDICTOR_ALL_WRONG_PATH", {0x20010} , 0xf0, 1, {0xffff0003}, NULL}, */
	{55, 55, str_nil, "BRANCH_PREDICTOR_ALL_WRONG_TARGET"},	/* { "BRANCH_PREDICTOR_ALL_WRONG_TARGET", {0x30010} , 0xf0, 1, {0xffff0003}, NULL}, */
	{56, 56, str_nil, "BRANCH_TAKEN_SLOT_0"},	/* { "BRANCH_TAKEN_SLOT_0", {0x1000d} , 0xf0, 1, {0xffff0003}, NULL}, */
	{57, 57, str_nil, "BRANCH_TAKEN_SLOT_1"},	/* { "BRANCH_TAKEN_SLOT_1", {0x2000d} , 0xf0, 1, {0xffff0003}, NULL}, */
	{58, 58, str_nil, "BRANCH_TAKEN_SLOT_2"},	/* { "BRANCH_TAKEN_SLOT_2", {0x4000d} , 0xf0, 1, {0xffff0003}, NULL}, */
	{59, 59, str_nil, "BUS_ALL_ANY"},	/* { "BUS_ALL_ANY", {0x10047} , 0xf0, 1, {0xffff0000}, NULL}, */
	{60, 60, str_nil, "BUS_ALL_IO"},	/* { "BUS_ALL_IO", {0x40047} , 0xf0, 1, {0xffff0000}, NULL}, */
	{61, 61, str_nil, "BUS_ALL_SELF"},	/* { "BUS_ALL_SELF", {0x20047} , 0xf0, 1, {0xffff0000}, NULL}, */
	{62, 62, str_nil, "BUS_BRQ_LIVE_REQ_HI"},	/* { "BUS_BRQ_LIVE_REQ_HI", {0x5c} , 0xf0, 2, {0xffff0000}, NULL}, */
	{63, 63, str_nil, "BUS_BRQ_LIVE_REQ_LO"},	/* { "BUS_BRQ_LIVE_REQ_LO", {0x5b} , 0xf0, 2, {0xffff0000}, NULL}, */
	{64, 64, str_nil, "BUS_BRQ_REQ_INSERTED"},	/* { "BUS_BRQ_REQ_INSERTED", {0x5d} , 0xf0, 1, {0xffff0000}, NULL}, */
	{65, 65, str_nil, "BUS_BURST_ANY"},	/* { "BUS_BURST_ANY", {0x10049} , 0xf0, 1, {0xffff0000}, NULL}, */
	{66, 66, str_nil, "BUS_BURST_IO"},	/* { "BUS_BURST_IO", {0x40049} , 0xf0, 1, {0xffff0000}, NULL}, */
	{67, 67, str_nil, "BUS_BURST_SELF"},	/* { "BUS_BURST_SELF", {0x20049} , 0xf0, 1, {0xffff0000}, NULL}, */
	{68, 68, str_nil, "BUS_HITM"},	/* { "BUS_HITM", {0x44} , 0xf0, 1, {0xffff0000}, NULL}, */
	{69, 69, str_nil, "BUS_IO_ANY"},	/* { "BUS_IO_ANY", {0x10050} , 0xf0, 1, {0xffff0000}, NULL}, */
	{70, 70, str_nil, "BUS_IOQ_LIVE_REQ_HI"},	/* { "BUS_IOQ_LIVE_REQ_HI", {0x58} , 0xf0, 3, {0xffff0000}, NULL}, */
	{71, 71, str_nil, "BUS_IOQ_LIVE_REQ_LO"},	/* { "BUS_IOQ_LIVE_REQ_LO", {0x57} , 0xf0, 3, {0xffff0000}, NULL}, */
	{72, 72, str_nil, "BUS_IO_SELF"},	/* { "BUS_IO_SELF", {0x20050} , 0xf0, 1, {0xffff0000}, NULL}, */
	{73, 73, str_nil, "BUS_LOCK_ANY"},	/* { "BUS_LOCK_ANY", {0x10053} , 0xf0, 1, {0xffff0000}, NULL}, */
	{74, 74, str_nil, "BUS_LOCK_CYCLES_ANY"},	/* { "BUS_LOCK_CYCLES_ANY", {0x10054} , 0xf0, 1, {0xffff0000}, NULL}, */
	{75, 75, str_nil, "BUS_LOCK_CYCLES_SELF"},	/* { "BUS_LOCK_CYCLES_SELF", {0x20054} , 0xf0, 1, {0xffff0000}, NULL}, */
	{76, 76, str_nil, "BUS_LOCK_SELF"},	/* { "BUS_LOCK_SELF", {0x20053} , 0xf0, 1, {0xffff0000}, NULL}, */
	{77, 77, str_nil, "BUS_MEMORY_ANY"},	/* { "BUS_MEMORY_ANY", {0x1004a} , 0xf0, 1, {0xffff0000}, NULL}, */
	{78, 78, str_nil, "BUS_MEMORY_IO"},	/* { "BUS_MEMORY_IO", {0x4004a} , 0xf0, 1, {0xffff0000}, NULL}, */
	{79, 79, str_nil, "BUS_MEMORY_SELF"},	/* { "BUS_MEMORY_SELF", {0x2004a} , 0xf0, 1, {0xffff0000}, NULL}, */
	{80, 80, str_nil, "BUS_PARTIAL_ANY"},	/* { "BUS_PARTIAL_ANY", {0x10048} , 0xf0, 1, {0xffff0000}, NULL}, */
	{81, 81, str_nil, "BUS_PARTIAL_IO"},	/* { "BUS_PARTIAL_IO", {0x40048} , 0xf0, 1, {0xffff0000}, NULL}, */
	{82, 82, str_nil, "BUS_PARTIAL_SELF"},	/* { "BUS_PARTIAL_SELF", {0x20048} , 0xf0, 1, {0xffff0000}, NULL}, */
	{83, 83, str_nil, "BUS_RD_ALL_ANY"},	/* { "BUS_RD_ALL_ANY", {0x1004b} , 0xf0, 1, {0xffff0000}, NULL}, */
	{84, 84, str_nil, "BUS_RD_ALL_IO"},	/* { "BUS_RD_ALL_IO", {0x4004b} , 0xf0, 1, {0xffff0000}, NULL}, */
	{85, 85, str_nil, "BUS_RD_ALL_SELF"},	/* { "BUS_RD_ALL_SELF", {0x2004b} , 0xf0, 1, {0xffff0000}, NULL}, */
	{86, 86, str_nil, "BUS_RD_DATA_ANY"},	/* { "BUS_RD_DATA_ANY", {0x1004c} , 0xf0, 1, {0xffff0000}, NULL}, */
	{87, 87, str_nil, "BUS_RD_DATA_IO"},	/* { "BUS_RD_DATA_IO", {0x4004c} , 0xf0, 1, {0xffff0000}, NULL}, */
	{88, 88, str_nil, "BUS_RD_DATA_SELF"},	/* { "BUS_RD_DATA_SELF", {0x2004c} , 0xf0, 1, {0xffff0000}, NULL}, */
	{89, 89, str_nil, "BUS_RD_HIT"},	/* { "BUS_RD_HIT", {0x40} , 0xf0, 1, {0xffff0000}, NULL}, */
	{90, 90, str_nil, "BUS_RD_HITM"},	/* { "BUS_RD_HITM", {0x41} , 0xf0, 1, {0xffff0000}, NULL}, */
	{91, 91, str_nil, "BUS_RD_INVAL_ANY"},	/* { "BUS_RD_INVAL_ANY", {0x1004e} , 0xf0, 1, {0xffff0000}, NULL}, */
	{92, 92, str_nil, "BUS_RD_INVAL_BST_ANY"},	/* { "BUS_RD_INVAL_BST_ANY", {0x1004f} , 0xf0, 1, {0xffff0000}, NULL}, */
	{93, 93, str_nil, "BUS_RD_INVAL_BST_HITM"},	/* { "BUS_RD_INVAL_BST_HITM", {0x43} , 0xf0, 1, {0xffff0000}, NULL}, */
	{94, 94, str_nil, "BUS_RD_INVAL_BST_IO"},	/* { "BUS_RD_INVAL_BST_IO", {0x4004f} , 0xf0, 1, {0xffff0000}, NULL}, */
	{95, 95, str_nil, "BUS_RD_INVAL_BST_SELF"},	/* { "BUS_RD_INVAL_BST_SELF", {0x2004f} , 0xf0, 1, {0xffff0000}, NULL}, */
	{96, 96, str_nil, "BUS_RD_INVAL_HITM"},	/* { "BUS_RD_INVAL_HITM", {0x42} , 0xf0, 1, {0xffff0000}, NULL}, */
	{97, 97, str_nil, "BUS_RD_INVAL_IO"},	/* { "BUS_RD_INVAL_IO", {0x4004e} , 0xf0, 1, {0xffff0000}, NULL}, */
	{98, 98, str_nil, "BUS_RD_INVAL_SELF"},	/* { "BUS_RD_INVAL_SELF", {0x2004e} , 0xf0, 1, {0xffff0000}, NULL}, */
	{99, 99, str_nil, "BUS_RD_IO_ANY"},	/* { "BUS_RD_IO_ANY", {0x10051} , 0xf0, 1, {0xffff0000}, NULL}, */
	{100, 100, str_nil, "BUS_RD_IO_SELF"},	/* { "BUS_RD_IO_SELF", {0x20051} , 0xf0, 1, {0xffff0000}, NULL}, */
	{101, 101, str_nil, "BUS_RD_PRTL_ANY"},	/* { "BUS_RD_PRTL_ANY", {0x1004d} , 0xf0, 1, {0xffff0000}, NULL}, */
	{102, 102, str_nil, "BUS_RD_PRTL_IO"},	/* { "BUS_RD_PRTL_IO", {0x4004d} , 0xf0, 1, {0xffff0000}, NULL}, */
	{103, 103, str_nil, "BUS_RD_PRTL_SELF"},	/* { "BUS_RD_PRTL_SELF", {0x2004d} , 0xf0, 1, {0xffff0000}, NULL}, */
	{104, 104, str_nil, "BUS_SNOOPQ_REQ"},	/* { "BUS_SNOOPQ_REQ", {0x56} , 0x30, 3, {0xffff0000}, NULL}, */
	{105, 105, str_nil, "BUS_SNOOPS_ANY"},	/* { "BUS_SNOOPS_ANY", {0x10046} , 0xf0, 1, {0xffff0000}, NULL}, */
	{106, 106, str_nil, "BUS_SNOOPS_HITM_ANY"},	/* { "BUS_SNOOPS_HITM_ANY", {0x10045} , 0xf0, 1, {0xffff0000}, NULL}, */
	{107, 107, str_nil, "BUS_SNOOP_STALL_CYCLES_ANY"},	/* { "BUS_SNOOP_STALL_CYCLES_ANY", {0x10055} , 0xf0, 1, {0xffff0000}, NULL}, */
	{108, 108, str_nil, "BUS_SNOOP_STALL_CYCLES_SELF"},	/* { "BUS_SNOOP_STALL_CYCLES_SELF", {0x20055} , 0xf0, 1, {0xffff0000}, NULL}, */
	{109, 109, str_nil, "BUS_WR_WB_ANY"},	/* { "BUS_WR_WB_ANY", {0x10052} , 0xf0, 1, {0xffff0000}, NULL}, */
	{110, 110, str_nil, "BUS_WR_WB_IO"},	/* { "BUS_WR_WB_IO", {0x40052} , 0xf0, 1, {0xffff0000}, NULL}, */
	{111, 111, str_nil, "BUS_WR_WB_SELF"},	/* { "BUS_WR_WB_SELF", {0x20052} , 0xf0, 1, {0xffff0000}, NULL}, */
	{112, 112, str_nil, "CPU_CPL_CHANGES"},	/* { "CPU_CPL_CHANGES", {0x34} , 0xf0, 1, {0xffff0000}, NULL}, */
	{113, 113, "cycles", "CPU_CYCLES"},	/* { "CPU_CYCLES", {0x12} , 0xf0, 1, {0xffff0000}, NULL}, */
	{114, 114, str_nil, "DATA_ACCESS_CYCLE"},	/* { "DATA_ACCESS_CYCLE", {0x3} , 0xf0, 1, {0xffff0000}, NULL}, */
	{115, 115, str_nil, "DATA_EAR_CACHE_LAT1024"},	/* { "DATA_EAR_CACHE_LAT1024", {0x90367} , 0xf0, 1, {0xffff0003}, NULL}, */
	{116, 116, str_nil, "DATA_EAR_CACHE_LAT128"},	/* { "DATA_EAR_CACHE_LAT128", {0x50367} , 0xf0, 1, {0xffff0003}, NULL}, */
	{117, 117, str_nil, "DATA_EAR_CACHE_LAT16"},	/* { "DATA_EAR_CACHE_LAT16", {0x20367} , 0xf0, 1, {0xffff0003}, NULL}, */
	{118, 118, str_nil, "DATA_EAR_CACHE_LAT2048"},	/* { "DATA_EAR_CACHE_LAT2048", {0xa0367} , 0xf0, 1, {0xffff0003}, NULL}, */
	{119, 119, str_nil, "DATA_EAR_CACHE_LAT256"},	/* { "DATA_EAR_CACHE_LAT256", {0x60367} , 0xf0, 1, {0xffff0003}, NULL}, */
	{120, 120, str_nil, "DATA_EAR_CACHE_LAT32"},	/* { "DATA_EAR_CACHE_LAT32", {0x30367} , 0xf0, 1, {0xffff0003}, NULL}, */
	{121, 121, str_nil, "DATA_EAR_CACHE_LAT4"},	/* { "DATA_EAR_CACHE_LAT4", {0x367} , 0xf0, 1, {0xffff0003}, NULL}, */
	{122, 122, str_nil, "DATA_EAR_CACHE_LAT512"},	/* { "DATA_EAR_CACHE_LAT512", {0x80367} , 0xf0, 1, {0xffff0003}, NULL}, */
	{123, 123, str_nil, "DATA_EAR_CACHE_LAT64"},	/* { "DATA_EAR_CACHE_LAT64", {0x40367} , 0xf0, 1, {0xffff0003}, NULL}, */
	{124, 124, str_nil, "DATA_EAR_CACHE_LAT8"},	/* { "DATA_EAR_CACHE_LAT8", {0x10367} , 0xf0, 1, {0xffff0003}, NULL}, */
	{125, 125, str_nil, "DATA_EAR_CACHE_LAT_NONE"},	/* { "DATA_EAR_CACHE_LAT_NONE", {0xf0367} , 0xf0, 1, {0xffff0003}, NULL}, */
	{126, 126, str_nil, "DATA_EAR_EVENTS"},	/* { "DATA_EAR_EVENTS", {0x67} , 0xf0, 1, {0xffff0007}, NULL}, */
	{127, 127, str_nil, "DATA_EAR_TLB_L2"},	/* { "DATA_EAR_TLB_L2", {0x20767} , 0xf0, 1, {0xffff0003}, NULL}, */
	{128, 128, str_nil, "DATA_EAR_TLB_SW"},	/* { "DATA_EAR_TLB_SW", {0x80767} , 0xf0, 1, {0xffff0003}, NULL}, */
	{129, 129, str_nil, "DATA_EAR_TLB_VHPT"},	/* { "DATA_EAR_TLB_VHPT", {0x40767} , 0xf0, 1, {0xffff0003}, NULL}, */
	{130, 130, str_nil, "DATA_REFERENCES_RETIRED"},	/* { "DATA_REFERENCES_RETIRED", {0x63} , 0xf0, 2, {0xffff0007}, NULL}, */
	{131, 131, str_nil, "DEPENDENCY_ALL_CYCLE"},	/* { "DEPENDENCY_ALL_CYCLE", {0x6} , 0xf0, 1, {0xffff0000}, NULL}, */
	{132, 132, str_nil, "DEPENDENCY_SCOREBOARD_CYCLE"},	/* { "DEPENDENCY_SCOREBOARD_CYCLE", {0x2} , 0xf0, 1, {0xffff0000}, NULL}, */
	{133, 133, str_nil, "DTC_MISSES"},	/* { "DTC_MISSES", {0x60} , 0xf0, 1, {0xffff0007}, NULL}, */
	{134, 134, str_nil, "DTLB_INSERTS_HPW"},	/* { "DTLB_INSERTS_HPW", {0x62} , 0xf0, 1, {0xffff0007}, NULL}, */
	{135, 135, "TLB_misses", "DTLB_MISSES"},	/* { "DTLB_MISSES", {0x61} , 0xf0, 1, {0xffff0007}, NULL}, */
	{136, 136, str_nil, "EXPL_STOPBITS"},	/* { "EXPL_STOPBITS", {0x2e} , 0xf0, 1, {0xffff0001}, NULL}, */
	{137, 137, str_nil, "FP_FLUSH_TO_ZERO"},	/* { "FP_FLUSH_TO_ZERO", {0xb} , 0xf0, 2, {0xffff0003}, NULL}, */
	{138, 138, str_nil, "FP_OPS_RETIRED_HI"},	/* { "FP_OPS_RETIRED_HI", {0xa} , 0xf0, 3, {0xffff0003}, NULL}, */
	{139, 139, str_nil, "FP_OPS_RETIRED_LO"},	/* { "FP_OPS_RETIRED_LO", {0x9} , 0xf0, 3, {0xffff0003}, NULL}, */
	{140, 140, str_nil, "FP_SIR_FLUSH"},	/* { "FP_SIR_FLUSH", {0xc} , 0xf0, 2, {0xffff0003}, NULL}, */
	{141, 141, str_nil, "IA32_INST_RETIRED"},	/* { "IA32_INST_RETIRED", {0x15} , 0xf0, 2, {0xffff0000}, NULL}, */
	{142, 142, str_nil, "IA64_INST_RETIRED"},	/* { "IA64_INST_RETIRED", {0x8} , 0x30, 6, {0xffff0003}, NULL}, */
	{143, 143, str_nil, "IA64_TAGGED_INST_RETIRED_PMC8"},	/* { "IA64_TAGGED_INST_RETIRED_PMC8", {0x30008} , 0x30, 6, {0xffff0003}, NULL}, */
	{144, 144, str_nil, "IA64_TAGGED_INST_RETIRED_PMC9"},	/* { "IA64_TAGGED_INST_RETIRED_PMC9", {0x20008} , 0x30, 6, {0xffff0003}, NULL}, */
	{145, 145, str_nil, "INST_ACCESS_CYCLE"},	/* { "INST_ACCESS_CYCLE", {0x1} , 0xf0, 1, {0xffff0000}, NULL}, */
	{146, 146, str_nil, "INST_DISPERSED"},	/* { "INST_DISPERSED", {0x2d} , 0x30, 6, {0xffff0001}, NULL}, */
	{147, 147, str_nil, "INST_FAILED_CHKS_RETIRED_ALL"},	/* { "INST_FAILED_CHKS_RETIRED_ALL", {0x30035} , 0xf0, 1, {0xffff0003}, NULL}, */
	{148, 148, str_nil, "INST_FAILED_CHKS_RETIRED_FP"},	/* { "INST_FAILED_CHKS_RETIRED_FP", {0x20035} , 0xf0, 1, {0xffff0003}, NULL}, */
	{149, 149, str_nil, "INST_FAILED_CHKS_RETIRED_INT"},	/* { "INST_FAILED_CHKS_RETIRED_INT", {0x10035} , 0xf0, 1, {0xffff0003}, NULL}, */
	{150, 150, str_nil, "INSTRUCTION_EAR_CACHE_LAT1024"},	/* { "INSTRUCTION_EAR_CACHE_LAT1024", {0x80123} , 0xf0, 1, {0xffff0001}, NULL}, */
	{151, 151, str_nil, "INSTRUCTION_EAR_CACHE_LAT128"},	/* { "INSTRUCTION_EAR_CACHE_LAT128", {0x50123} , 0xf0, 1, {0xffff0001}, NULL}, */
	{152, 152, str_nil, "INSTRUCTION_EAR_CACHE_LAT16"},	/* { "INSTRUCTION_EAR_CACHE_LAT16", {0x20123} , 0xf0, 1, {0xffff0001}, NULL}, */
	{153, 153, str_nil, "INSTRUCTION_EAR_CACHE_LAT2048"},	/* { "INSTRUCTION_EAR_CACHE_LAT2048", {0x90123} , 0xf0, 1, {0xffff0001}, NULL}, */
	{154, 154, str_nil, "INSTRUCTION_EAR_CACHE_LAT256"},	/* { "INSTRUCTION_EAR_CACHE_LAT256", {0x60123} , 0xf0, 1, {0xffff0001}, NULL}, */
	{155, 155, str_nil, "INSTRUCTION_EAR_CACHE_LAT32"},	/* { "INSTRUCTION_EAR_CACHE_LAT32", {0x30123} , 0xf0, 1, {0xffff0001}, NULL}, */
	{156, 156, str_nil, "INSTRUCTION_EAR_CACHE_LAT4096"},	/* { "INSTRUCTION_EAR_CACHE_LAT4096", {0xa0123} , 0xf0, 1, {0xffff0001}, NULL}, */
	{157, 157, str_nil, "INSTRUCTION_EAR_CACHE_LAT4"},	/* { "INSTRUCTION_EAR_CACHE_LAT4", {0x123} , 0xf0, 1, {0xffff0001}, NULL}, */
	{158, 158, str_nil, "INSTRUCTION_EAR_CACHE_LAT512"},	/* { "INSTRUCTION_EAR_CACHE_LAT512", {0x70123} , 0xf0, 1, {0xffff0001}, NULL}, */
	{159, 159, str_nil, "INSTRUCTION_EAR_CACHE_LAT64"},	/* { "INSTRUCTION_EAR_CACHE_LAT64", {0x40123} , 0xf0, 1, {0xffff0001}, NULL}, */
	{160, 160, str_nil, "INSTRUCTION_EAR_CACHE_LAT8"},	/* { "INSTRUCTION_EAR_CACHE_LAT8", {0x10123} , 0xf0, 1, {0xffff0001}, NULL}, */
	{161, 161, str_nil, "INSTRUCTION_EAR_CACHE_LAT_NONE"},	/* { "INSTRUCTION_EAR_CACHE_LAT_NONE", {0xf0123} , 0xf0, 1, {0xffff0001}, NULL}, */
	{162, 162, str_nil, "INSTRUCTION_EAR_EVENTS"},	/* { "INSTRUCTION_EAR_EVENTS", {0x23} , 0xf0, 1, {0xffff0001}, NULL}, */
	{163, 163, str_nil, "INSTRUCTION_EAR_TLB_SW"},	/* { "INSTRUCTION_EAR_TLB_SW", {0x80523} , 0xf0, 1, {0xffff0001}, NULL}, */
	{164, 164, str_nil, "INSTRUCTION_EAR_TLB_VHPT"},	/* { "INSTRUCTION_EAR_TLB_VHPT", {0x40523} , 0xf0, 1, {0xffff0001}, NULL}, */
	{165, 165, str_nil, "ISA_TRANSITIONS"},	/* { "ISA_TRANSITIONS", {0x14} , 0xf0, 1, {0xffff0000}, NULL}, */
	{166, 166, str_nil, "ISB_LINES_IN"},	/* { "ISB_LINES_IN", {0x26} , 0xf0, 1, {0xffff0000}, NULL}, */
	{167, 167, str_nil, "ITLB_INSERTS_HPW"},	/* { "ITLB_INSERTS_HPW", {0x28} , 0xf0, 1, {0xffff0001}, NULL}, */
	{168, 168, "iTLB_misses", "ITLB_MISSES_FETCH"},	/* { "ITLB_MISSES_FETCH", {0x27} , 0xf0, 1, {0xffff0001}, NULL}, */
	{169, 169, str_nil, "L1D_READ_FORCED_MISSES_RETIRED"},	/* { "L1D_READ_FORCED_MISSES_RETIRED", {0x6b} , 0xf0, 2, {0xffff0007}, NULL}, */
	{170, 170, "L1_data_misses", "L1D_READ_MISSES_RETIRED"},	/* { "L1D_READ_MISSES_RETIRED", {0x66} , 0xf0, 2, {0xffff0007}, NULL}, */
	{171, 171, str_nil, "L1D_READS_RETIRED"},	/* { "L1D_READS_RETIRED", {0x64} , 0xf0, 2, {0xffff0007}, NULL}, */
	{172, 172, str_nil, "L1I_DEMAND_READS"},	/* { "L1I_DEMAND_READS", {0x20} , 0xf0, 1, {0xffff0001}, NULL}, */
	{173, 173, str_nil, "L1I_FILLS"},	/* { "L1I_FILLS", {0x21} , 0xf0, 1, {0xffff0000}, NULL}, */
	{174, 174, str_nil, "L1I_PREFETCH_READS"},	/* { "L1I_PREFETCH_READS", {0x24} , 0xf0, 1, {0xffff0001}, NULL}, */
	{175, 175, str_nil, "L1_OUTSTANDING_REQ_HI"},	/* { "L1_OUTSTANDING_REQ_HI", {0x79} , 0xf0, 1, {0xffff0000}, NULL}, */
	{176, 176, str_nil, "L1_OUTSTANDING_REQ_LO"},	/* { "L1_OUTSTANDING_REQ_LO", {0x78} , 0xf0, 1, {0xffff0000}, NULL}, */
	{177, 177, str_nil, "L2_DATA_REFERENCES_ALL"},	/* { "L2_DATA_REFERENCES_ALL", {0x30069} , 0xf0, 2, {0xffff0007}, NULL}, */
	{178, 178, str_nil, "L2_DATA_REFERENCES_READS"},	/* { "L2_DATA_REFERENCES_READS", {0x10069} , 0xf0, 2, {0xffff0007}, NULL}, */
	{179, 179, str_nil, "L2_DATA_REFERENCES_WRITES"},	/* { "L2_DATA_REFERENCES_WRITES", {0x20069} , 0xf0, 2, {0xffff0007}, NULL}, */
	{180, 180, str_nil, "L2_FLUSH_DETAILS_ADDR_CONFLICT"},	/* { "L2_FLUSH_DETAILS_ADDR_CONFLICT", {0x20077} , 0xf0, 1, {0xffff0000}, NULL}, */
	{181, 181, str_nil, "L2_FLUSH_DETAILS_ALL"},	/* { "L2_FLUSH_DETAILS_ALL", {0xf0077} , 0xf0, 1, {0xffff0000}, NULL}, */
	{182, 182, str_nil, "L2_FLUSH_DETAILS_BUS_REJECT"},	/* { "L2_FLUSH_DETAILS_BUS_REJECT", {0x40077} , 0xf0, 1, {0xffff0000}, NULL}, */
	{183, 183, str_nil, "L2_FLUSH_DETAILS_FULL_FLUSH"},	/* { "L2_FLUSH_DETAILS_FULL_FLUSH", {0x80077} , 0xf0, 1, {0xffff0000}, NULL}, */
	{184, 184, str_nil, "L2_FLUSH_DETAILS_ST_BUFFER"},	/* { "L2_FLUSH_DETAILS_ST_BUFFER", {0x10077} , 0xf0, 1, {0xffff0000}, NULL}, */
	{185, 185, str_nil, "L2_FLUSHES"},	/* { "L2_FLUSHES", {0x76} , 0xf0, 1, {0xffff0000}, NULL}, */
	{186, 186, str_nil, "L2_INST_DEMAND_READS"},	/* { "L2_INST_DEMAND_READS", {0x22} , 0xf0, 1, {0xffff0001}, NULL}, */
	{187, 187, str_nil, "L2_INST_PREFETCH_READS"},	/* { "L2_INST_PREFETCH_READS", {0x25} , 0xf0, 1, {0xffff0001}, NULL}, */
	{188, 188, "L2_data_misses", "L2_MISSES"},	/* { "L2_MISSES", {0x6a} , 0xf0, 2, {0xffff0007}, NULL}, */
	{189, 189, str_nil, "L2_REFERENCES"},	/* { "L2_REFERENCES", {0x68} , 0xf0, 3, {0xffff0007}, NULL}, */
	{190, 190, str_nil, "L3_LINES_REPLACED"},	/* { "L3_LINES_REPLACED", {0x7f} , 0xf0, 1, {0xffff0000}, NULL}, */
	{191, 191, "L3_data_misses", "L3_MISSES"},	/* { "L3_MISSES", {0x7c} , 0xf0, 1, {0xffff0000}, NULL}, */
	{192, 192, str_nil, "L3_READS_ALL_READS_ALL"},	/* { "L3_READS_ALL_READS_ALL", {0xf007d} , 0xf0, 1, {0xffff0000}, NULL}, */
	{193, 193, str_nil, "L3_READS_ALL_READS_HIT"},	/* { "L3_READS_ALL_READS_HIT", {0xd007d} , 0xf0, 1, {0xffff0000}, NULL}, */
	{194, 194, str_nil, "L3_READS_ALL_READS_MISS"},	/* { "L3_READS_ALL_READS_MISS", {0xe007d} , 0xf0, 1, {0xffff0000}, NULL}, */
	{195, 195, str_nil, "L3_READS_DATA_READS_ALL"},	/* { "L3_READS_DATA_READS_ALL", {0xb007d} , 0xf0, 1, {0xffff0000}, NULL}, */
	{196, 196, str_nil, "L3_READS_DATA_READS_HIT"},	/* { "L3_READS_DATA_READS_HIT", {0x9007d} , 0xf0, 1, {0xffff0000}, NULL}, */
	{197, 197, str_nil, "L3_READS_DATA_READS_MISS"},	/* { "L3_READS_DATA_READS_MISS", {0xa007d} , 0xf0, 1, {0xffff0000}, NULL}, */
	{198, 198, str_nil, "L3_READS_INST_READS_ALL"},	/* { "L3_READS_INST_READS_ALL", {0x7007d} , 0xf0, 1, {0xffff0000}, NULL}, */
	{199, 199, str_nil, "L3_READS_INST_READS_HIT"},	/* { "L3_READS_INST_READS_HIT", {0x5007d} , 0xf0, 1, {0xffff0000}, NULL}, */
	{200, 200, str_nil, "L3_READS_INST_READS_MISS"},	/* { "L3_READS_INST_READS_MISS", {0x6007d} , 0xf0, 1, {0xffff0000}, NULL}, */
	{201, 201, str_nil, "L3_REFERENCES"},	/* { "L3_REFERENCES", {0x7b} , 0xf0, 1, {0xffff0007}, NULL}, */
	{202, 202, str_nil, "L3_WRITES_ALL_WRITES_ALL"},	/* { "L3_WRITES_ALL_WRITES_ALL", {0xf007e} , 0xf0, 1, {0xffff0000}, NULL}, */
	{203, 203, str_nil, "L3_WRITES_ALL_WRITES_HIT"},	/* { "L3_WRITES_ALL_WRITES_HIT", {0xd007e} , 0xf0, 1, {0xffff0000}, NULL}, */
	{204, 204, str_nil, "L3_WRITES_ALL_WRITES_MISS"},	/* { "L3_WRITES_ALL_WRITES_MISS", {0xe007e} , 0xf0, 1, {0xffff0000}, NULL}, */
	{205, 205, str_nil, "L3_WRITES_DATA_WRITES_ALL"},	/* { "L3_WRITES_DATA_WRITES_ALL", {0x7007e} , 0xf0, 1, {0xffff0000}, NULL}, */
	{206, 206, str_nil, "L3_WRITES_DATA_WRITES_HIT"},	/* { "L3_WRITES_DATA_WRITES_HIT", {0x5007e} , 0xf0, 1, {0xffff0000}, NULL}, */
	{207, 207, str_nil, "L3_WRITES_DATA_WRITES_MISS"},	/* { "L3_WRITES_DATA_WRITES_MISS", {0x6007e} , 0xf0, 1, {0xffff0000}, NULL}, */
	{208, 208, str_nil, "L3_WRITES_L2_WRITEBACK_ALL"},	/* { "L3_WRITES_L2_WRITEBACK_ALL", {0xb007e} , 0xf0, 1, {0xffff0000}, NULL}, */
	{209, 209, str_nil, "L3_WRITES_L2_WRITEBACK_HIT"},	/* { "L3_WRITES_L2_WRITEBACK_HIT", {0x9007e} , 0xf0, 1, {0xffff0000}, NULL}, */
	{210, 210, str_nil, "L3_WRITES_L2_WRITEBACK_MISS"},	/* { "L3_WRITES_L2_WRITEBACK_MISS", {0xa007e} , 0xf0, 1, {0xffff0000}, NULL}, */
	{211, 211, str_nil, "LOADS_RETIRED"},	/* { "LOADS_RETIRED", {0x6c} , 0xf0, 2, {0xffff0007}, NULL}, */
	{212, 212, str_nil, "MEMORY_CYCLE"},	/* { "MEMORY_CYCLE", {0x7} , 0xf0, 1, {0xffff0000}, NULL}, */
	{213, 213, str_nil, "MISALIGNED_LOADS_RETIRED"},	/* { "MISALIGNED_LOADS_RETIRED", {0x70} , 0xf0, 2, {0xffff0007}, NULL}, */
	{214, 214, str_nil, "MISALIGNED_STORES_RETIRED"},	/* { "MISALIGNED_STORES_RETIRED", {0x71} , 0xf0, 2, {0xffff0007}, NULL}, */
	{215, 215, str_nil, "NOPS_RETIRED"},	/* { "NOPS_RETIRED", {0x30} , 0x30, 6, {0xffff0003}, NULL}, */
	{216, 216, str_nil, "PIPELINE_ALL_FLUSH_CYCLE"},	/* { "PIPELINE_ALL_FLUSH_CYCLE", {0x4} , 0xf0, 1, {0xffff0000}, NULL}, */
	{217, 217, str_nil, "PIPELINE_BACKEND_FLUSH_CYCLE"},	/* { "PIPELINE_BACKEND_FLUSH_CYCLE", {0x0} , 0xf0, 1, {0xffff0000}, NULL}, */
	{218, 218, str_nil, "PIPELINE_FLUSH_ALL"},	/* { "PIPELINE_FLUSH_ALL", {0xf0033} , 0xf0, 1, {0xffff0000}, NULL}, */
	{219, 219, str_nil, "PIPELINE_FLUSH_DTC_FLUSH"},	/* { "PIPELINE_FLUSH_DTC_FLUSH", {0x40033} , 0xf0, 1, {0xffff0000}, NULL}, */
	{220, 220, str_nil, "PIPELINE_FLUSH_IEU_FLUSH"},	/* { "PIPELINE_FLUSH_IEU_FLUSH", {0x80033} , 0xf0, 1, {0xffff0000}, NULL}, */
	{221, 221, str_nil, "PIPELINE_FLUSH_L1D_WAYMP_FLUSH"},	/* { "PIPELINE_FLUSH_L1D_WAYMP_FLUSH", {0x20033} , 0xf0, 1, {0xffff0000}, NULL}, */
	{222, 222, str_nil, "PIPELINE_FLUSH_OTHER_FLUSH"},	/* { "PIPELINE_FLUSH_OTHER_FLUSH", {0x10033} , 0xf0, 1, {0xffff0000}, NULL}, */
	{223, 223, str_nil, "PREDICATE_SQUASHED_RETIRED"},	/* { "PREDICATE_SQUASHED_RETIRED", {0x31} , 0x30, 6, {0xffff0003}, NULL}, */
	{224, 224, str_nil, "RSE_LOADS_RETIRED"},	/* { "RSE_LOADS_RETIRED", {0x72} , 0xf0, 2, {0xffff0007}, NULL}, */
	{225, 225, str_nil, "RSE_REFERENCES_RETIRED"},	/* { "RSE_REFERENCES_RETIRED", {0x65} , 0xf0, 2, {0xffff0007}, NULL}, */
	{226, 226, str_nil, "STORES_RETIRED"},	/* { "STORES_RETIRED", {0x6d} , 0xf0, 2, {0xffff0007}, NULL}, */
	{227, 227, str_nil, "UC_LOADS_RETIRED"},	/* { "UC_LOADS_RETIRED", {0x6e} , 0xf0, 2, {0xffff0007}, NULL}, */
	{228, 228, str_nil, "UC_STORES_RETIRED"},	/* { "UC_STORES_RETIRED", {0x6f} , 0xf0, 2, {0xffff0007}, NULL}, */
	{229, 229, str_nil, "UNSTALLED_BACKEND_CYCLE"},	/* { "UNSTALLED_BACKEND_CYCLE", {0x5} , 0xf0, 1, {0xffff0000}, NULL}, */
	{113, 113, str_nil, str_nil}
};

  /* I2 (ia64) Itanium2 */

#define I2_NUMEVENTS 475
event_t I2_event[I2_NUMEVENTS + 1] = {
	{0, 0, str_nil, "ALAT_CAPACITY_MISS_ALL"},	/* { "ALAT_CAPACITY_MISS_ALL", {0x30058}, 0xf0, 2, {0xf00007}, "ALAT Entry Replaced -- both integer and floating point instructions"}, */
	{1, 1, str_nil, "ALAT_CAPACITY_MISS_FP"},	/* { "ALAT_CAPACITY_MISS_FP", {0x20058}, 0xf0, 2, {0xf00007}, "ALAT Entry Replaced -- only floating point instructions"}, */
	{2, 2, str_nil, "ALAT_CAPACITY_MISS_INT"},	/* { "ALAT_CAPACITY_MISS_INT", {0x10058}, 0xf0, 2, {0xf00007}, "ALAT Entry Replaced -- only integer instructions"}, */
	{3, 3, str_nil, "BACK_END_BUBBLE_ALL"},	/* { "BACK_END_BUBBLE_ALL", {0x0}, 0xf0, 1, {0xf00000}, "Full Pipe Bubbles in Main Pipe -- Front-end, RSE, EXE, FPU/L1D stall or a pipeline flush due to an exception/branch misprediction"}, */
	{4, 4, str_nil, "BACK_END_BUBBLE_FE"},	/* { "BACK_END_BUBBLE_FE", {0x10000}, 0xf0, 1, {0xf00000}, "Full Pipe Bubbles in Main Pipe -- front-end"}, */
	{5, 5, str_nil, "BACK_END_BUBBLE_L1D_FPU_RSE"},	/* { "BACK_END_BUBBLE_L1D_FPU_RSE", {0x20000}, 0xf0, 1, {0xf00000}, "Full Pipe Bubbles in Main Pipe -- L1D_FPU or RSE."}, */
	{6, 6, str_nil, "BE_BR_MISPRED_DETAIL_ANY"},	/* { "BE_BR_MISPRED_DETAIL_ANY", {0x61}, 0xf0, 1, {0xf00003}, "BE Branch Misprediction Detail -- any back-end (be) mispredictions"}, */
	{7, 7, str_nil, "BE_BR_MISPRED_DETAIL_PFS"},	/* { "BE_BR_MISPRED_DETAIL_PFS", {0x30061}, 0xf0, 1, {0xf00003}, "BE Branch Misprediction Detail -- only back-end pfs mispredictions for taken branches"}, */
	{8, 8, str_nil, "BE_BR_MISPRED_DETAIL_ROT"},	/* { "BE_BR_MISPRED_DETAIL_ROT", {0x20061}, 0xf0, 1, {0xf00003}, "BE Branch Misprediction Detail -- only back-end rotate mispredictions"}, */
	{9, 9, str_nil, "BE_BR_MISPRED_DETAIL_STG"},	/* { "BE_BR_MISPRED_DETAIL_STG", {0x10061}, 0xf0, 1, {0xf00003}, "BE Branch Misprediction Detail -- only back-end stage mispredictions"}, */
	{10, 10, str_nil, "BE_EXE_BUBBLE_ALL"},	/* { "BE_EXE_BUBBLE_ALL", {0x2}, 0xf0, 1, {0xf00000}, "Full Pipe Bubbles in Main Pipe due to Execution Unit Stalls -- Back-end was stalled by exe"}, */
	{11, 11, str_nil, "BE_EXE_BUBBLE_ARCR"},	/* { "BE_EXE_BUBBLE_ARCR", {0x40002}, 0xf0, 1, {0xf00000}, "Full Pipe Bubbles in Main Pipe due to Execution Unit Stalls -- Back-end was stalled by exe due to AR or CR dependency"}, */
	{12, 12, str_nil, "BE_EXE_BUBBLE_ARCR_PR_CANCEL_BANK"},	/* { "BE_EXE_BUBBLE_ARCR_PR_CANCEL_BANK", {0x80002}, 0xf0, 1, {0xf00000}, "Full Pipe Bubbles in Main Pipe due to Execution Unit Stalls -- ARCR, PR, CANCEL or BANK_SWITCH"}, */
	{13, 13, str_nil, "BE_EXE_BUBBLE_BANK_SWITCH"},	/* { "BE_EXE_BUBBLE_BANK_SWITCH", {0x70002}, 0xf0, 1, {0xf00000}, "Full Pipe Bubbles in Main Pipe due to Execution Unit Stalls -- Back-end was stalled by exe due to bank switching."}, */
	{14, 14, str_nil, "BE_EXE_BUBBLE_CANCEL"},	/* { "BE_EXE_BUBBLE_CANCEL", {0x60002}, 0xf0, 1, {0xf00000}, "Full Pipe Bubbles in Main Pipe due to Execution Unit Stalls -- Back-end was stalled by exe due to a canceled load"}, */
	{15, 15, str_nil, "BE_EXE_BUBBLE_FRALL"},	/* { "BE_EXE_BUBBLE_FRALL", {0x20002}, 0xf0, 1, {0xf00000}, "Full Pipe Bubbles in Main Pipe due to Execution Unit Stalls -- Back-end was stalled by exe due to FR/FR or FR/load dependency"}, */
	{16, 16, str_nil, "BE_EXE_BUBBLE_GRALL"},	/* { "BE_EXE_BUBBLE_GRALL", {0x10002}, 0xf0, 1, {0xf00000}, "Full Pipe Bubbles in Main Pipe due to Execution Unit Stalls -- Back-end was stalled by exe due to GR/GR or GR/load dependency"}, */
	{17, 17, str_nil, "BE_EXE_BUBBLE_GRGR"},	/* { "BE_EXE_BUBBLE_GRGR", {0x50002}, 0xf0, 1, {0xf00000}, "Full Pipe Bubbles in Main Pipe due to Execution Unit Stalls -- Back-end was stalled by exe due to GR/GR dependency"}, */
	{18, 18, str_nil, "BE_EXE_BUBBLE_PR"},	/* { "BE_EXE_BUBBLE_PR", {0x30002}, 0xf0, 1, {0xf00000}, "Full Pipe Bubbles in Main Pipe due to Execution Unit Stalls -- Back-end was stalled by exe due to PR dependency"}, */
	{19, 19, str_nil, "BE_FLUSH_BUBBLE_ALL"},	/* { "BE_FLUSH_BUBBLE_ALL", {0x4}, 0xf0, 1, {0xf00000}, "Full Pipe Bubbles in Main Pipe due to Flushes. -- Back-end was stalled due to either an exception/interruption or branch misprediction flush"}, */
	{20, 20, str_nil, "BE_FLUSH_BUBBLE_BRU"},	/* { "BE_FLUSH_BUBBLE_BRU", {0x10004}, 0xf0, 1, {0xf00000}, "Full Pipe Bubbles in Main Pipe due to Flushes. -- Back-end was stalled due to a branch misprediction flush"}, */
	{21, 21, str_nil, "BE_FLUSH_BUBBLE_XPN"},	/* { "BE_FLUSH_BUBBLE_XPN", {0x20004}, 0xf0, 1, {0xf00000}, "Full Pipe Bubbles in Main Pipe due to Flushes. -- Back-end was stalled due to an exception/interruption flush"}, */
	{22, 22, str_nil, "BE_L1D_FPU_BUBBLE_ALL"},	/* { "BE_L1D_FPU_BUBBLE_ALL", {0xca}, 0xf0, 1, {0x5210000}, "Full Pipe Bubbles in Main Pipe due to FPU or L1D Cache -- Back-end was stalled by L1D or FPU"}, */
	{23, 23, str_nil, "BE_L1D_FPU_BUBBLE_FPU"},	/* { "BE_L1D_FPU_BUBBLE_FPU", {0x100ca}, 0xf0, 1, {0x5210000}, "Full Pipe Bubbles in Main Pipe due to FPU or L1D Cache -- Back-end was stalled by FPU."}, */
	{24, 24, str_nil, "BE_L1D_FPU_BUBBLE_L1D"},	/* { "BE_L1D_FPU_BUBBLE_L1D", {0x200ca}, 0xf0, 1, {0x5210000}, "Full Pipe Bubbles in Main Pipe due to FPU or L1D Cache -- Back-end was stalled by L1D. This includes all stalls caused by the L1 pipeline (created in the L1D stage of the L1 pipeline which corresponds to the DET stage of the main pipe)."}, */
	{25, 25, str_nil, "BE_L1D_FPU_BUBBLE_L1D_DCS"},	/* { "BE_L1D_FPU_BUBBLE_L1D_DCS", {0x800ca}, 0xf0, 1, {0x5210000}, "Full Pipe Bubbles in Main Pipe due to FPU or L1D Cache -- Back-end was stalled by L1D due to DCS requiring a stall"}, */
	{26, 26, str_nil, "BE_L1D_FPU_BUBBLE_L1D_DCURECIR"},	/* { "BE_L1D_FPU_BUBBLE_L1D_DCURECIR", {0x400ca}, 0xf0, 1, {0x5210000}, "Full Pipe Bubbles in Main Pipe due to FPU or L1D Cache -- Back-end was stalled by L1D due to DCU recirculating"}, */
	{27, 27, str_nil, "BE_L1D_FPU_BUBBLE_L1D_FILLCONF"},	/* { "BE_L1D_FPU_BUBBLE_L1D_FILLCONF", {0x700ca}, 0xf0, 1, {0x5210000}, "Full Pipe Bubbles in Main Pipe due to FPU or L1D Cache -- Back-end was stalled by L1D due a store in conflict with a returning fill."}, */
	{28, 28, str_nil, "BE_L1D_FPU_BUBBLE_L1D_FULLSTBUF"},	/* { "BE_L1D_FPU_BUBBLE_L1D_FULLSTBUF", {0x300ca}, 0xf0, 1, {0x5210000}, "Full Pipe Bubbles in Main Pipe due to FPU or L1D Cache -- Back-end was stalled by L1D due to store buffer being full"}, */
	{29, 29, str_nil, "BE_L1D_FPU_BUBBLE_L1D_HPW"},	/* { "BE_L1D_FPU_BUBBLE_L1D_HPW", {0x500ca}, 0xf0, 1, {0x5210000}, "Full Pipe Bubbles in Main Pipe due to FPU or L1D Cache -- Back-end was stalled by L1D due to Hardware Page Walker"}, */
	{30, 30, str_nil, "BE_L1D_FPU_BUBBLE_L1D_L2BPRESS"},	/* { "BE_L1D_FPU_BUBBLE_L1D_L2BPRESS", {0x900ca}, 0xf0, 1, {0x5210000}, "Full Pipe Bubbles in Main Pipe due to FPU or L1D Cache -- Back-end was stalled by L1D due to L2 Back Pressure"}, */
	{31, 31, str_nil, "BE_L1D_FPU_BUBBLE_L1D_LDCHK"},	/* { "BE_L1D_FPU_BUBBLE_L1D_LDCHK", {0xc00ca}, 0xf0, 1, {0x5210000}, "Full Pipe Bubbles in Main Pipe due to FPU or L1D Cache -- Back-end was stalled by L1D due to architectural ordering conflict"}, */
	{32, 32, str_nil, "BE_L1D_FPU_BUBBLE_L1D_LDCONF"},	/* { "BE_L1D_FPU_BUBBLE_L1D_LDCONF", {0xb00ca}, 0xf0, 1, {0x5210000}, "Full Pipe Bubbles in Main Pipe due to FPU or L1D Cache -- Back-end was stalled by L1D due to architectural ordering conflict"}, */
	{33, 33, str_nil, "BE_L1D_FPU_BUBBLE_L1D_NAT"},	/* { "BE_L1D_FPU_BUBBLE_L1D_NAT", {0xd00ca}, 0xf0, 1, {0x5210000}, "Full Pipe Bubbles in Main Pipe due to FPU or L1D Cache -- Back-end was stalled by L1D due to L1D data return needing recirculated NaT generation."}, */
	{34, 34, str_nil, "BE_L1D_FPU_BUBBLE_L1D_NATCONF"},	/* { "BE_L1D_FPU_BUBBLE_L1D_NATCONF", {0xf00ca}, 0xf0, 1, {0x5210000}, "Full Pipe Bubbles in Main Pipe due to FPU or L1D Cache -- Back-end was stalled by L1D due to ld8.fill conflict with st8.spill not written to unat."}, */
	{35, 35, str_nil, "BE_L1D_FPU_BUBBLE_L1D_STBUFRECIR"},	/* { "BE_L1D_FPU_BUBBLE_L1D_STBUFRECIR", {0xe00ca}, 0xf0, 1, {0x5210000}, "Full Pipe Bubbles in Main Pipe due to FPU or L1D Cache -- Back-end was stalled by L1D due to store buffer cancel needing recirculate."}, */
	{36, 36, str_nil, "BE_L1D_FPU_BUBBLE_L1D_TLB"},	/* { "BE_L1D_FPU_BUBBLE_L1D_TLB", {0xa00ca}, 0xf0, 1, {0x5210000}, "Full Pipe Bubbles in Main Pipe due to FPU or L1D Cache -- Back-end was stalled by L1D due to L2DTLB to L1DTLB transfer"}, */
	{37, 37, str_nil, "BE_LOST_BW_DUE_TO_FE_ALL"},	/* { "BE_LOST_BW_DUE_TO_FE_ALL", {0x72}, 0xf0, 2, {0xf00000}, "Invalid Bundles if BE Not Stalled for Other Reasons. -- count regardless of cause"}, */
	{38, 38, str_nil, "BE_LOST_BW_DUE_TO_FE_BI"},	/* { "BE_LOST_BW_DUE_TO_FE_BI", {0x90072}, 0xf0, 2, {0xf00000}, "Invalid Bundles if BE Not Stalled for Other Reasons. -- only if caused by branch initialization stall"}, */
	{39, 39, str_nil, "BE_LOST_BW_DUE_TO_FE_BRQ"},	/* { "BE_LOST_BW_DUE_TO_FE_BRQ", {0xa0072}, 0xf0, 2, {0xf00000}, "Invalid Bundles if BE Not Stalled for Other Reasons. -- only if caused by branch retirement queue stall"}, */
	{40, 40, str_nil, "BE_LOST_BW_DUE_TO_FE_BR_ILOCK"},	/* { "BE_LOST_BW_DUE_TO_FE_BR_ILOCK", {0xc0072}, 0xf0, 2, {0xf00000}, "Invalid Bundles if BE Not Stalled for Other Reasons. -- only if caused by branch interlock stall"}, */
	{41, 41, str_nil, "BE_LOST_BW_DUE_TO_FE_BUBBLE"},	/* { "BE_LOST_BW_DUE_TO_FE_BUBBLE", {0xd0072}, 0xf0, 2, {0xf00000}, "Invalid Bundles if BE Not Stalled for Other Reasons. -- only if caused by branch resteer bubble stall"}, */
	{42, 42, str_nil, "BE_LOST_BW_DUE_TO_FE_FEFLUSH"},	/* { "BE_LOST_BW_DUE_TO_FE_FEFLUSH", {0x10072}, 0xf0, 2, {0xf00000}, "Invalid Bundles if BE Not Stalled for Other Reasons. -- only if caused by a front-end flush"}, */
	{43, 43, str_nil, "BE_LOST_BW_DUE_TO_FE_FILL_RECIRC"},	/* { "BE_LOST_BW_DUE_TO_FE_FILL_RECIRC", {0x80072}, 0xf0, 2, {0xf00000}, "Invalid Bundles if BE Not Stalled for Other Reasons. -- only if caused by a recirculate for a cache line fill operation"}, */
	{44, 44, str_nil, "BE_LOST_BW_DUE_TO_FE_IBFULL"},	/* { "BE_LOST_BW_DUE_TO_FE_IBFULL", {0x50072}, 0xf0, 2, {0xf00000}, "Invalid Bundles if BE Not Stalled for Other Reasons. -- (* meaningless for this event *)"}, */
	{45, 45, str_nil, "BE_LOST_BW_DUE_TO_FE_IMISS"},	/* { "BE_LOST_BW_DUE_TO_FE_IMISS", {0x60072}, 0xf0, 2, {0xf00000}, "Invalid Bundles if BE Not Stalled for Other Reasons. -- only if caused by instruction cache miss stall"}, */
	{46, 46, str_nil, "BE_LOST_BW_DUE_TO_FE_PLP"},	/* { "BE_LOST_BW_DUE_TO_FE_PLP", {0xb0072}, 0xf0, 2, {0xf00000}, "Invalid Bundles if BE Not Stalled for Other Reasons. -- only if caused by perfect loop prediction stall"}, */
	{47, 47, str_nil, "BE_LOST_BW_DUE_TO_FE_TLBMISS"},	/* { "BE_LOST_BW_DUE_TO_FE_TLBMISS", {0x70072}, 0xf0, 2, {0xf00000}, "Invalid Bundles if BE Not Stalled for Other Reasons. -- only if caused by TLB stall"}, */
	{48, 48, str_nil, "BE_LOST_BW_DUE_TO_FE_UNREACHED"},	/* { "BE_LOST_BW_DUE_TO_FE_UNREACHED", {0x40072}, 0xf0, 2, {0xf00000}, "Invalid Bundles if BE Not Stalled for Other Reasons. -- only if caused by unreachable bundle"}, */
	{49, 49, str_nil, "BE_RSE_BUBBLE_ALL"},	/* { "BE_RSE_BUBBLE_ALL", {0x1}, 0xf0, 1, {0xf00000}, "Full Pipe Bubbles in Main Pipe due to RSE Stalls -- Back-end was stalled by RSE"}, */
	{50, 50, str_nil, "BE_RSE_BUBBLE_AR_DEP"},	/* { "BE_RSE_BUBBLE_AR_DEP", {0x20001}, 0xf0, 1, {0xf00000}, "Full Pipe Bubbles in Main Pipe due to RSE Stalls -- Back-end was stalled by RSE due to AR dependencies"}, */
	{51, 51, str_nil, "BE_RSE_BUBBLE_BANK_SWITCH"},	/* { "BE_RSE_BUBBLE_BANK_SWITCH", {0x10001}, 0xf0, 1, {0xf00000}, "Full Pipe Bubbles in Main Pipe due to RSE Stalls -- Back-end was stalled by RSE due to bank switching"}, */
	{52, 52, str_nil, "BE_RSE_BUBBLE_LOADRS"},	/* { "BE_RSE_BUBBLE_LOADRS", {0x50001}, 0xf0, 1, {0xf00000}, "Full Pipe Bubbles in Main Pipe due to RSE Stalls -- Back-end was stalled by RSE due to loadrs calculations"}, */
	{53, 53, str_nil, "BE_RSE_BUBBLE_OVERFLOW"},	/* { "BE_RSE_BUBBLE_OVERFLOW", {0x30001}, 0xf0, 1, {0xf00000}, "Full Pipe Bubbles in Main Pipe due to RSE Stalls -- Back-end was stalled by RSE due to need to spill"}, */
	{54, 54, str_nil, "BE_RSE_BUBBLE_UNDERFLOW"},	/* { "BE_RSE_BUBBLE_UNDERFLOW", {0x40001}, 0xf0, 1, {0xf00000}, "Full Pipe Bubbles in Main Pipe due to RSE Stalls -- Back-end was stalled by RSE due to need to fill"}, */
	{55, 55, str_nil, "BRANCH_EVENT"},	/* { "BRANCH_EVENT", {0x111}, 0xf0, 1, {0xf00003}, "Branch Event Captured"}, */
	{56, 56, str_nil, "BR_MISPRED_DETAIL_ALL_ALL_PRED"},	/* { "BR_MISPRED_DETAIL_ALL_ALL_PRED", {0x5b}, 0xf0, 3, {0xf00003}, "FE Branch Mispredict Detail -- All branch types regardless of prediction result"}, */
	{57, 57, str_nil, "BR_MISPRED_DETAIL_ALL_CORRECT_PRED"},	/* { "BR_MISPRED_DETAIL_ALL_CORRECT_PRED", {0x1005b}, 0xf0, 3, {0xf00003}, "FE Branch Mispredict Detail -- All branch types, correctly predicted branches (outcome and target)"}, */
	{58, 58, str_nil, "BR_MISPRED_DETAIL_ALL_WRONG_PATH"},	/* { "BR_MISPRED_DETAIL_ALL_WRONG_PATH", {0x2005b}, 0xf0, 3, {0xf00003}, "FE Branch Mispredict Detail -- All branch types, mispredicted branches due to wrong branch direction"}, */
	{59, 59, str_nil, "BR_MISPRED_DETAIL_ALL_WRONG_TARGET"},	/* { "BR_MISPRED_DETAIL_ALL_WRONG_TARGET", {0x3005b}, 0xf0, 3, {0xf00003}, "FE Branch Mispredict Detail -- All branch types, mispredicted branches due to wrong target for taken branches"}, */
	{60, 60, str_nil, "BR_MISPRED_DETAIL_IPREL_ALL_PRED"},	/* { "BR_MISPRED_DETAIL_IPREL_ALL_PRED", {0x4005b}, 0xf0, 3, {0xf00003}, "FE Branch Mispredict Detail -- Only IP relative branches, regardless of prediction result"}, */
	{61, 61, str_nil, "BR_MISPRED_DETAIL_IPREL_CORRECT_PRED"},	/* { "BR_MISPRED_DETAIL_IPREL_CORRECT_PRED", {0x5005b}, 0xf0, 3, {0xf00003}, "FE Branch Mispredict Detail -- Only IP relative branches, correctly predicted branches (outcome and target)"}, */
	{62, 62, str_nil, "BR_MISPRED_DETAIL_IPREL_WRONG_PATH"},	/* { "BR_MISPRED_DETAIL_IPREL_WRONG_PATH", {0x6005b}, 0xf0, 3, {0xf00003}, "FE Branch Mispredict Detail -- Only IP relative branches, mispredicted branches due to wrong branch direction"}, */
	{63, 63, str_nil, "BR_MISPRED_DETAIL_IPREL_WRONG_TARGET"},	/* { "BR_MISPRED_DETAIL_IPREL_WRONG_TARGET", {0x7005b}, 0xf0, 3, {0xf00003}, "FE Branch Mispredict Detail -- Only IP relative branches, mispredicted branches due to wrong target for taken branches"}, */
	{64, 64, str_nil, "BR_MISPRED_DETAIL_NTRETIND_ALL_PRED"},	/* { "BR_MISPRED_DETAIL_NTRETIND_ALL_PRED", {0xc005b}, 0xf0, 3, {0xf00003}, "FE Branch Mispredict Detail -- Only non-return indirect branches, regardless of prediction result"}, */
	{65, 65, str_nil, "BR_MISPRED_DETAIL_NTRETIND_CORRECT_PRED"},	/* { "BR_MISPRED_DETAIL_NTRETIND_CORRECT_PRED", {0xd005b}, 0xf0, 3, {0xf00003}, "FE Branch Mispredict Detail -- Only non-return indirect branches, correctly predicted branches (outcome and target)"}, */
	{66, 66, str_nil, "BR_MISPRED_DETAIL_NTRETIND_WRONG_PATH"},	/* { "BR_MISPRED_DETAIL_NTRETIND_WRONG_PATH", {0xe005b}, 0xf0, 3, {0xf00003}, "FE Branch Mispredict Detail -- Only non-return indirect branches, mispredicted branches due to wrong branch direction"}, */
	{67, 67, str_nil, "BR_MISPRED_DETAIL_NTRETIND_WRONG_TARGET"},	/* { "BR_MISPRED_DETAIL_NTRETIND_WRONG_TARGET", {0xf005b}, 0xf0, 3, {0xf00003}, "FE Branch Mispredict Detail -- Only non-return indirect branches, mispredicted branches due to wrong target for taken branches"}, */
	{68, 68, str_nil, "BR_MISPRED_DETAIL_RETURN_ALL_PRED"},	/* { "BR_MISPRED_DETAIL_RETURN_ALL_PRED", {0x8005b}, 0xf0, 3, {0xf00003}, "FE Branch Mispredict Detail -- Only return type branches, regardless of prediction result"}, */
	{69, 69, str_nil, "BR_MISPRED_DETAIL_RETURN_CORRECT_PRED"},	/* { "BR_MISPRED_DETAIL_RETURN_CORRECT_PRED", {0x9005b}, 0xf0, 3, {0xf00003}, "FE Branch Mispredict Detail -- Only return type branches, correctly predicted branches (outcome and target)"}, */
	{70, 70, str_nil, "BR_MISPRED_DETAIL_RETURN_WRONG_PATH"},	/* { "BR_MISPRED_DETAIL_RETURN_WRONG_PATH", {0xa005b}, 0xf0, 3, {0xf00003}, "FE Branch Mispredict Detail -- Only return type branches, mispredicted branches due to wrong branch direction"}, */
	{71, 71, str_nil, "BR_MISPRED_DETAIL_RETURN_WRONG_TARGET"},	/* { "BR_MISPRED_DETAIL_RETURN_WRONG_TARGET", {0xb005b}, 0xf0, 3, {0xf00003}, "FE Branch Mispredict Detail -- Only return type branches, mispredicted branches due to wrong target for taken branches"}, */
	{72, 72, str_nil, "BR_MISPRED_DETAIL2_ALL_ALL_UNKNOWN_PRED"},	/* { "BR_MISPRED_DETAIL2_ALL_ALL_UNKNOWN_PRED", {0x68}, 0xf0, 2, {0xf00003}, "FE Branch Mispredict Detail (Unknown Path Component) -- All branch types, branches with unknown path prediction"}, */
	{73, 73, str_nil, "BR_MISPRED_DETAIL2_ALL_UNKNOWN_PATH_CORRECT_PRED"},	/* { "BR_MISPRED_DETAIL2_ALL_UNKNOWN_PATH_CORRECT_PRED", {0x10068}, 0xf0, 2, {0xf00003}, "FE Branch Mispredict Detail (Unknown Path Component) -- All branch types, branches with unknown path prediction and correctly predicted branch (outcome & target)"}, */
	{74, 74, str_nil, "BR_MISPRED_DETAIL2_ALL_UNKNOWN_PATH_WRONG_PATH"},	/* { "BR_MISPRED_DETAIL2_ALL_UNKNOWN_PATH_WRONG_PATH", {0x20068}, 0xf0, 2, {0xf00003}, "FE Branch Mispredict Detail (Unknown Path Component) -- All branch types, branches with unknown path prediction and wrong branch direction"}, */
	{75, 75, str_nil, "BR_MISPRED_DETAIL2_IPREL_ALL_UNKNOWN_PRED"},	/* { "BR_MISPRED_DETAIL2_IPREL_ALL_UNKNOWN_PRED", {0x40068}, 0xf0, 2, {0xf00003}, "FE Branch Mispredict Detail (Unknown Path Component) -- Only IP relative branches, branches with unknown path prediction"}, */
	{76, 76, str_nil, "BR_MISPRED_DETAIL2_IPREL_UNKNOWN_PATH_CORRECT_PRED"},	/* { "BR_MISPRED_DETAIL2_IPREL_UNKNOWN_PATH_CORRECT_PRED", {0x50068}, 0xf0, 2, {0xf00003}, "FE Branch Mispredict Detail (Unknown Path Component) -- Only IP relative branches, branches with unknown path prediction and correct predicted branch (outcome & target)"}, */
	{77, 77, str_nil, "BR_MISPRED_DETAIL2_IPREL_UNKNOWN_PATH_WRONG_PATH"},	/* { "BR_MISPRED_DETAIL2_IPREL_UNKNOWN_PATH_WRONG_PATH", {0x60068}, 0xf0, 2, {0xf00003}, "FE Branch Mispredict Detail (Unknown Path Component) -- Only IP relative branches, branches with unknown path prediction and wrong branch direction"}, */
	{78, 78, str_nil, "BR_MISPRED_DETAIL2_NRETIND_ALL_UNKNOWN_PRED"},	/* { "BR_MISPRED_DETAIL2_NRETIND_ALL_UNKNOWN_PRED", {0xc0068}, 0xf0, 2, {0xf00003}, "FE Branch Mispredict Detail (Unknown Path Component) -- Only non-return indirect branches, branches with unknown path prediction"}, */
	{79, 79, str_nil, "BR_MISPRED_DETAIL2_NRETIND_UNKNOWN_PATH_CORRECT_PRED"},	/* { "BR_MISPRED_DETAIL2_NRETIND_UNKNOWN_PATH_CORRECT_PRED", {0xd0068}, 0xf0, 2, {0xf00003}, "FE Branch Mispredict Detail (Unknown Path Component) -- Only non-return indirect branches, branches with unknown path prediction and correct predicted branch (outcome & target)"}, */
	{80, 80, str_nil, "BR_MISPRED_DETAIL2_NRETIND_UNKNOWN_PATH_WRONG_PATH"},	/* { "BR_MISPRED_DETAIL2_NRETIND_UNKNOWN_PATH_WRONG_PATH", {0xe0068}, 0xf0, 2, {0xf00003}, "FE Branch Mispredict Detail (Unknown Path Component) -- Only non-return indirect branches, branches with unknown path prediction and wrong branch direction"}, */
	{81, 81, str_nil, "BR_MISPRED_DETAIL2_RETURN_ALL_UNKNOWN_PRED"},	/* { "BR_MISPRED_DETAIL2_RETURN_ALL_UNKNOWN_PRED", {0x80068}, 0xf0, 2, {0xf00003}, "FE Branch Mispredict Detail (Unknown Path Component) -- Only return type branches, branches with unknown path prediction"}, */
	{82, 82, str_nil, "BR_MISPRED_DETAIL2_RETURN_UNKNOWN_PATH_CORRECT_PRED"},	/* { "BR_MISPRED_DETAIL2_RETURN_UNKNOWN_PATH_CORRECT_PRED", {0x90068}, 0xf0, 2, {0xf00003}, "FE Branch Mispredict Detail (Unknown Path Component) -- Only return type branches, branches with unknown path prediction and correct predicted branch (outcome & target)"}, */
	{83, 83, str_nil, "BR_MISPRED_DETAIL2_RETURN_UNKNOWN_PATH_WRONG_PATH"},	/* { "BR_MISPRED_DETAIL2_RETURN_UNKNOWN_PATH_WRONG_PATH", {0xa0068}, 0xf0, 2, {0xf00003}, "FE Branch Mispredict Detail (Unknown Path Component) -- Only return type branches, branches with unknown path prediction and wrong branch direction"}, */
	{84, 84, str_nil, "BR_PATH_PRED_ALL_MISPRED_NOTTAKEN"},	/* { "BR_PATH_PRED_ALL_MISPRED_NOTTAKEN", {0x54}, 0xf0, 3, {0xf00003}, "FE Branch Path Prediction Detail -- All branch types, incorrectly predicted path and not taken branch"}, */
	{85, 85, str_nil, "BR_PATH_PRED_ALL_MISPRED_TAKEN"},	/* { "BR_PATH_PRED_ALL_MISPRED_TAKEN", {0x10054}, 0xf0, 3, {0xf00003}, "FE Branch Path Prediction Detail -- All branch types, incorrectly predicted path and taken branch"}, */
	{86, 86, str_nil, "BR_PATH_PRED_ALL_OKPRED_NOTTAKEN"},	/* { "BR_PATH_PRED_ALL_OKPRED_NOTTAKEN", {0x20054}, 0xf0, 3, {0xf00003}, "FE Branch Path Prediction Detail -- All branch types, correctly predicted path and not taken branch"}, */
	{87, 87, str_nil, "BR_PATH_PRED_ALL_OKPRED_TAKEN"},	/* { "BR_PATH_PRED_ALL_OKPRED_TAKEN", {0x30054}, 0xf0, 3, {0xf00003}, "FE Branch Path Prediction Detail -- All branch types, correctly predicted path and taken branch"}, */
	{88, 88, str_nil, "BR_PATH_PRED_IPREL_MISPRED_NOTTAKEN"},	/* { "BR_PATH_PRED_IPREL_MISPRED_NOTTAKEN", {0x40054}, 0xf0, 3, {0xf00003}, "FE Branch Path Prediction Detail -- Only IP relative branches, incorrectly predicted path and not taken branch"}, */
	{89, 89, str_nil, "BR_PATH_PRED_IPREL_MISPRED_TAKEN"},	/* { "BR_PATH_PRED_IPREL_MISPRED_TAKEN", {0x50054}, 0xf0, 3, {0xf00003}, "FE Branch Path Prediction Detail -- Only IP relative branches, incorrectly predicted path and taken branch"}, */
	{90, 90, str_nil, "BR_PATH_PRED_IPREL_OKPRED_NOTTAKEN"},	/* { "BR_PATH_PRED_IPREL_OKPRED_NOTTAKEN", {0x60054}, 0xf0, 3, {0xf00003}, "FE Branch Path Prediction Detail -- Only IP relative branches, correctly predicted path and not taken branch"}, */
	{91, 91, str_nil, "BR_PATH_PRED_IPREL_OKPRED_TAKEN"},	/* { "BR_PATH_PRED_IPREL_OKPRED_TAKEN", {0x70054}, 0xf0, 3, {0xf00003}, "FE Branch Path Prediction Detail -- Only IP relative branches, correctly predicted path and taken branch"}, */
	{92, 92, str_nil, "BR_PATH_PRED_NRETIND_MISPRED_NOTTAKEN"},	/* { "BR_PATH_PRED_NRETIND_MISPRED_NOTTAKEN", {0xc0054}, 0xf0, 3, {0xf00003}, "FE Branch Path Prediction Detail -- Only non-return indirect branches, incorrectly predicted path and not taken branch"}, */
	{93, 93, str_nil, "BR_PATH_PRED_NRETIND_MISPRED_TAKEN"},	/* { "BR_PATH_PRED_NRETIND_MISPRED_TAKEN", {0xd0054}, 0xf0, 3, {0xf00003}, "FE Branch Path Prediction Detail -- Only non-return indirect branches, incorrectly predicted path and taken branch"}, */
	{94, 94, str_nil, "BR_PATH_PRED_NRETIND_OKPRED_NOTTAKEN"},	/* { "BR_PATH_PRED_NRETIND_OKPRED_NOTTAKEN", {0xe0054}, 0xf0, 3, {0xf00003}, "FE Branch Path Prediction Detail -- Only non-return indirect branches, correctly predicted path and not taken branch"}, */
	{95, 95, str_nil, "BR_PATH_PRED_NRETIND_OKPRED_TAKEN"},	/* { "BR_PATH_PRED_NRETIND_OKPRED_TAKEN", {0xf0054}, 0xf0, 3, {0xf00003}, "FE Branch Path Prediction Detail -- Only non-return indirect branches, correctly predicted path and taken branch"}, */
	{96, 96, str_nil, "BR_PATH_PRED_RETURN_MISPRED_NOTTAKEN"},	/* { "BR_PATH_PRED_RETURN_MISPRED_NOTTAKEN", {0x80054}, 0xf0, 3, {0xf00003}, "FE Branch Path Prediction Detail -- Only return type branches, incorrectly predicted path and not taken branch"}, */
	{97, 97, str_nil, "BR_PATH_PRED_RETURN_MISPRED_TAKEN"},	/* { "BR_PATH_PRED_RETURN_MISPRED_TAKEN", {0x90054}, 0xf0, 3, {0xf00003}, "FE Branch Path Prediction Detail -- Only return type branches, incorrectly predicted path and taken branch"}, */
	{98, 98, str_nil, "BR_PATH_PRED_RETURN_OKPRED_NOTTAKEN"},	/* { "BR_PATH_PRED_RETURN_OKPRED_NOTTAKEN", {0xa0054}, 0xf0, 3, {0xf00003}, "FE Branch Path Prediction Detail -- Only return type branches, correctly predicted path and not taken branch"}, */
	{99, 99, str_nil, "BR_PATH_PRED_RETURN_OKPRED_TAKEN"},	/* { "BR_PATH_PRED_RETURN_OKPRED_TAKEN", {0xb0054}, 0xf0, 3, {0xf00003}, "FE Branch Path Prediction Detail -- Only return type branches, correctly predicted path and taken branch"}, */
	{100, 100, str_nil, "BR_PATH_PRED2_ALL_UNKNOWNPRED_NOTTAKEN"},	/* { "BR_PATH_PRED2_ALL_UNKNOWNPRED_NOTTAKEN", {0x6a}, 0xf0, 2, {0xf00003}, "FE Branch Path Prediction Detail (Unknown pred component) -- All branch types, unknown predicted path and not taken branch (which impacts OKPRED_NOTTAKEN)"}, */
	{101, 101, str_nil, "BR_PATH_PRED2_ALL_UNKNOWNPRED_TAKEN"},	/* { "BR_PATH_PRED2_ALL_UNKNOWNPRED_TAKEN", {0x1006a}, 0xf0, 2, {0xf00003}, "FE Branch Path Prediction Detail (Unknown pred component) -- All branch types, unknown predicted path and taken branch (which impacts MISPRED_TAKEN)"}, */
	{102, 102, str_nil, "BR_PATH_PRED2_IPREL_UNKNOWNPRED_NOTTAKEN"},	/* { "BR_PATH_PRED2_IPREL_UNKNOWNPRED_NOTTAKEN", {0x4006a}, 0xf0, 2, {0xf00003}, "FE Branch Path Prediction Detail (Unknown pred component) -- Only IP relative branches, unknown predicted path and not taken branch (which impacts OKPRED_NOTTAKEN)"}, */
	{103, 103, str_nil, "BR_PATH_PRED2_IPREL_UNKNOWNPRED_TAKEN"},	/* { "BR_PATH_PRED2_IPREL_UNKNOWNPRED_TAKEN", {0x5006a}, 0xf0, 2, {0xf00003}, "FE Branch Path Prediction Detail (Unknown pred component) -- Only IP relative branches, unknown predicted path and taken branch (which impacts MISPRED_TAKEN)"}, */
	{104, 104, str_nil, "BR_PATH_PRED2_NRETIND_UNKNOWNPRED_NOTTAKEN"},	/* { "BR_PATH_PRED2_NRETIND_UNKNOWNPRED_NOTTAKEN", {0xc006a}, 0xf0, 2, {0xf00003}, "FE Branch Path Prediction Detail (Unknown pred component) -- Only non-return indirect branches, unknown predicted path and not taken branch (which impacts OKPRED_NOTTAKEN)"}, */
	{105, 105, str_nil, "BR_PATH_PRED2_NRETIND_UNKNOWNPRED_TAKEN"},	/* { "BR_PATH_PRED2_NRETIND_UNKNOWNPRED_TAKEN", {0xd006a}, 0xf0, 2, {0xf00003}, "FE Branch Path Prediction Detail (Unknown pred component) -- Only non-return indirect branches, unknown predicted path and taken branch (which impacts MISPRED_TAKEN)"}, */
	{106, 106, str_nil, "BR_PATH_PRED2_RETURN_UNKNOWNPRED_NOTTAKEN"},	/* { "BR_PATH_PRED2_RETURN_UNKNOWNPRED_NOTTAKEN", {0x8006a}, 0xf0, 2, {0xf00003}, "FE Branch Path Prediction Detail (Unknown pred component) -- Only return type branches, unknown predicted path and not taken branch (which impacts OKPRED_NOTTAKEN)"}, */
	{107, 107, str_nil, "BR_PATH_PRED2_RETURN_UNKNOWNPRED_TAKEN"},	/* { "BR_PATH_PRED2_RETURN_UNKNOWNPRED_TAKEN", {0x9006a}, 0xf0, 2, {0xf00003}, "FE Branch Path Prediction Detail (Unknown pred component) -- Only return type branches, unknown predicted path and taken branch (which impacts MISPRED_TAKEN)"}, */
	{108, 108, str_nil, "BUS_ALL_ANY"},	/* { "BUS_ALL_ANY", {0x30087}, 0xf0, 1, {0xf00000}, "Bus Transactions -- CPU or non-CPU (all transactions)."}, */
	{109, 109, str_nil, "BUS_ALL_IO"},	/* { "BUS_ALL_IO", {0x10087}, 0xf0, 1, {0xf00000}, "Bus Transactions -- non-CPU priority agents"}, */
	{110, 110, str_nil, "BUS_ALL_SELF"},	/* { "BUS_ALL_SELF", {0x20087}, 0xf0, 1, {0xf00000}, "Bus Transactions -- local processor"}, */
	{111, 111, str_nil, "BUS_BACKSNP_REQ_THIS"},	/* { "BUS_BACKSNP_REQ_THIS", {0x1008e}, 0xf0, 1, {0xf00000}, "Bus Back Snoop Requests -- Counts the number of bus back snoop me requests"}, */
	{112, 112, str_nil, "BUS_BRQ_LIVE_REQ_HI"},	/* { "BUS_BRQ_LIVE_REQ_HI", {0x9c}, 0xf0, 2, {0xf00000}, "BRQ Live Requests (upper 2 bits)"}, */
	{113, 113, str_nil, "BUS_BRQ_LIVE_REQ_LO"},	/* { "BUS_BRQ_LIVE_REQ_LO", {0x9b}, 0xf0, 7, {0xf00000}, "BRQ Live Requests (lower 3 bits)"}, */
	{114, 114, str_nil, "BUS_BRQ_REQ_INSERTED"},	/* { "BUS_BRQ_REQ_INSERTED", {0x9d}, 0xf0, 1, {0xf00000}, "BRQ Requests Inserted"}, */
	{115, 115, str_nil, "BUS_DATA_CYCLE"},	/* { "BUS_DATA_CYCLE", {0x88}, 0xf0, 1, {0xf00000}, "Valid Data Cycle on the Bus"}, */
	{116, 116, str_nil, "BUS_HITM"},	/* { "BUS_HITM", {0x84}, 0xf0, 1, {0xf00000}, "Bus Hit Modified Line Transactions"}, */
	{117, 117, str_nil, "BUS_IO_ANY"},	/* { "BUS_IO_ANY", {0x30090}, 0xf0, 1, {0xf00000}, "IA-32 Compatible IO Bus Transactions -- CPU or non-CPU (all transactions)."}, */
	{118, 118, str_nil, "BUS_IO_IO"},	/* { "BUS_IO_IO", {0x10090}, 0xf0, 1, {0xf00000}, "IA-32 Compatible IO Bus Transactions -- non-CPU priority agents"}, */
	{119, 119, str_nil, "BUS_IO_SELF"},	/* { "BUS_IO_SELF", {0x20090}, 0xf0, 1, {0xf00000}, "IA-32 Compatible IO Bus Transactions -- local processor"}, */
	{120, 120, str_nil, "BUS_IOQ_LIVE_REQ_HI"},	/* { "BUS_IOQ_LIVE_REQ_HI", {0x98}, 0xf0, 2, {0xf00000}, "Inorder Bus Queue Requests (upper 2 bits)"}, */
	{121, 121, str_nil, "BUS_IOQ_LIVE_REQ_LO"},	/* { "BUS_IOQ_LIVE_REQ_LO", {0x97}, 0xf0, 3, {0xf00000}, "Inorder Bus Queue Requests (lower2 bitst)"}, */
	{122, 122, str_nil, "BUS_LOCK_ANY"},	/* { "BUS_LOCK_ANY", {0x30093}, 0xf0, 1, {0xf00000}, "IA-32 Compatible Bus Lock Transactions -- CPU or non-CPU (all transactions)."}, */
	{123, 123, str_nil, "BUS_LOCK_SELF"},	/* { "BUS_LOCK_SELF", {0x20093}, 0xf0, 1, {0xf00000}, "IA-32 Compatible Bus Lock Transactions -- local processor"}, */
	{124, 124, str_nil, "BUS_MEMORY_ALL_ANY"},	/* { "BUS_MEMORY_ALL_ANY", {0xf008a}, 0xf0, 1, {0xf00000}, "Bus Memory Transactions -- All bus transactions from CPU or non-CPU (all transactions)."}, */
	{125, 125, str_nil, "BUS_MEMORY_ALL_IO"},	/* { "BUS_MEMORY_ALL_IO", {0xd008a}, 0xf0, 1, {0xf00000}, "Bus Memory Transactions -- All bus transactions from non-CPU priority agents"}, */
	{126, 126, str_nil, "BUS_MEMORY_ALL_SELF"},	/* { "BUS_MEMORY_ALL_SELF", {0xe008a}, 0xf0, 1, {0xf00000}, "Bus Memory Transactions -- All bus transactions from local processor"}, */
	{127, 127, str_nil, "BUS_MEMORY_EQ_128BYTE_ANY"},	/* { "BUS_MEMORY_EQ_128BYTE_ANY", {0x7008a}, 0xf0, 1, {0xf00000}, "Bus Memory Transactions -- number of full cache line transactions (BRL, BRIL, BWL) from CPU or non-CPU (all transactions)."}, */
	{128, 128, str_nil, "BUS_MEMORY_EQ_128BYTE_IO"},	/* { "BUS_MEMORY_EQ_128BYTE_IO", {0x5008a}, 0xf0, 1, {0xf00000}, "Bus Memory Transactions -- number of full cache line transactions (BRL, BRIL, BWL) from non-CPU priority agents"}, */
	{129, 129, str_nil, "BUS_MEMORY_EQ_128BYTE_SELF"},	/* { "BUS_MEMORY_EQ_128BYTE_SELF", {0x6008a}, 0xf0, 1, {0xf00000}, "Bus Memory Transactions -- number of full cache line transactions (BRL, BRIL, BWL) from local processor"}, */
	{130, 130, str_nil, "BUS_MEMORY_LT_128BYTE_ANY"},	/* { "BUS_MEMORY_LT_128BYTE_ANY", {0xb008a}, 0xf0, 1, {0xf00000}, "Bus Memory Transactions -- number of less than full cache line transactions (BRP, BWP) CPU or non-CPU (all transactions)."}, */
	{131, 131, str_nil, "BUS_MEMORY_LT_128BYTE_IO"},	/* { "BUS_MEMORY_LT_128BYTE_IO", {0x9008a}, 0xf0, 1, {0xf00000}, "Bus Memory Transactions -- number of less than full cache line transactions (BRP, BWP) from non-CPU priority agents"}, */
	{132, 132, str_nil, "BUS_MEMORY_LT_128BYTE_SELF"},	/* { "BUS_MEMORY_LT_128BYTE_SELF", {0xa008a}, 0xf0, 1, {0xf00000}, "Bus Memory Transactions -- number of less than full cache line transactions (BRP, BWP) local processor"}, */
	{133, 133, str_nil, "BUS_MEM_READ_ALL_ANY"},	/* { "BUS_MEM_READ_ALL_ANY", {0xf008b}, 0xf0, 1, {0xf00000}, "Full Cache Line D/I Memory RD, RD Invalidate, and BRIL -- All memory read transactions from CPU or non-CPU (all transactions)."}, */
	{134, 134, str_nil, "BUS_MEM_READ_ALL_IO"},	/* { "BUS_MEM_READ_ALL_IO", {0xd008b}, 0xf0, 1, {0xf00000}, "Full Cache Line D/I Memory RD, RD Invalidate, and BRIL -- All memory read transactions from non-CPU priority agents"}, */
	{135, 135, str_nil, "BUS_MEM_READ_ALL_SELF"},	/* { "BUS_MEM_READ_ALL_SELF", {0xe008b}, 0xf0, 1, {0xf00000}, "Full Cache Line D/I Memory RD, RD Invalidate, and BRIL -- All memory read transactions from local processor"}, */
	{136, 136, str_nil, "BUS_MEM_READ_BIL_ANY"},	/* { "BUS_MEM_READ_BIL_ANY", {0x3008b}, 0xf0, 1, {0xf00000}, "Full Cache Line D/I Memory RD, RD Invalidate, and BRIL -- Number of BIL 0-byte memory read invalidate transactions from CPU or non-CPU (all transactions)."}, */
	{137, 137, str_nil, "BUS_MEM_READ_BIL_IO"},	/* { "BUS_MEM_READ_BIL_IO", {0x1008b}, 0xf0, 1, {0xf00000}, "Full Cache Line D/I Memory RD, RD Invalidate, and BRIL -- Number of BIL 0-byte memory read invalidate transactions from non-CPU priority agents"}, */
	{138, 138, str_nil, "BUS_MEM_READ_BIL_SELF"},	/* { "BUS_MEM_READ_BIL_SELF", {0x2008b}, 0xf0, 1, {0xf00000}, "Full Cache Line D/I Memory RD, RD Invalidate, and BRIL -- Number of BIL 0-byte memory read invalidate transactions from local processor"}, */
	{139, 139, str_nil, "BUS_MEM_READ_BRIL_ANY"},	/* { "BUS_MEM_READ_BRIL_ANY", {0xb008b}, 0xf0, 1, {0xf00000}, "Full Cache Line D/I Memory RD, RD Invalidate, and BRIL -- Number of full cache line memory read invalidate transactions from CPU or non-CPU (all transactions)."}, */
	{140, 140, str_nil, "BUS_MEM_READ_BRIL_IO"},	/* { "BUS_MEM_READ_BRIL_IO", {0x9008b}, 0xf0, 1, {0xf00000}, "Full Cache Line D/I Memory RD, RD Invalidate, and BRIL -- Number of full cache line memory read invalidate transactions from non-CPU priority agents"}, */
	{141, 141, str_nil, "BUS_MEM_READ_BRIL_SELF"},	/* { "BUS_MEM_READ_BRIL_SELF", {0xa008b}, 0xf0, 1, {0xf00000}, "Full Cache Line D/I Memory RD, RD Invalidate, and BRIL -- Number of full cache line memory read invalidate transactions from local processor"}, */
	{142, 142, str_nil, "BUS_MEM_READ_BRL_ANY"},	/* { "BUS_MEM_READ_BRL_ANY", {0x7008b}, 0xf0, 1, {0xf00000}, "Full Cache Line D/I Memory RD, RD Invalidate, and BRIL -- Number of full cache line memory read transactions from CPU or non-CPU (all transactions)."}, */
	{143, 143, str_nil, "BUS_MEM_READ_BRL_IO"},	/* { "BUS_MEM_READ_BRL_IO", {0x5008b}, 0xf0, 1, {0xf00000}, "Full Cache Line D/I Memory RD, RD Invalidate, and BRIL -- Number of full cache line memory read transactions from non-CPU priority agents"}, */
	{144, 144, str_nil, "BUS_MEM_READ_BRL_SELF"},	/* { "BUS_MEM_READ_BRL_SELF", {0x6008b}, 0xf0, 1, {0xf00000}, "Full Cache Line D/I Memory RD, RD Invalidate, and BRIL -- Number of full cache line memory read transactions from local processor"}, */
	{145, 145, str_nil, "BUS_MEM_READ_OUT_HI"},	/* { "BUS_MEM_READ_OUT_HI", {0x94}, 0xf0, 2, {0xf00000}, "Outstanding Memory Read Transactions (upper 2 bits)"}, */
	{146, 146, str_nil, "BUS_MEM_READ_OUT_LO"},	/* { "BUS_MEM_READ_OUT_LO", {0x95}, 0xf0, 7, {0xf00000}, "Outstanding Memory Read Transactions (lower 3 bits)"}, */
	{147, 147, str_nil, "BUS_OOQ_LIVE_REQ_HI"},	/* { "BUS_OOQ_LIVE_REQ_HI", {0x9a}, 0xf0, 2, {0xf00000}, "Out-of-order Bus Queue Requests (upper 2 bits)"}, */
	{148, 148, str_nil, "BUS_OOQ_LIVE_REQ_LO"},	/* { "BUS_OOQ_LIVE_REQ_LO", {0x99}, 0xf0, 7, {0xf00000}, "Out-of-order Bus Queue Requests (lower 3 bits)"}, */
	{149, 149, str_nil, "BUS_RD_DATA_ANY"},	/* { "BUS_RD_DATA_ANY", {0x3008c}, 0xf0, 1, {0xf00000}, "Bus Read Data Transactions -- CPU or non-CPU (all transactions)."}, */
	{150, 150, str_nil, "BUS_RD_DATA_IO"},	/* { "BUS_RD_DATA_IO", {0x1008c}, 0xf0, 1, {0xf00000}, "Bus Read Data Transactions -- non-CPU priority agents"}, */
	{151, 151, str_nil, "BUS_RD_DATA_SELF"},	/* { "BUS_RD_DATA_SELF", {0x2008c}, 0xf0, 1, {0xf00000}, "Bus Read Data Transactions -- local processor"}, */
	{152, 152, str_nil, "BUS_RD_HIT"},	/* { "BUS_RD_HIT", {0x80}, 0xf0, 1, {0xf00000}, "Bus Read Hit Clean Non-local Cache Transactions"}, */
	{153, 153, str_nil, "BUS_RD_HITM"},	/* { "BUS_RD_HITM", {0x81}, 0xf0, 1, {0xf00000}, "Bus Read Hit Modified Non-local Cache Transactions"}, */
	{154, 154, str_nil, "BUS_RD_INVAL_ALL_HITM"},	/* { "BUS_RD_INVAL_ALL_HITM", {0x83}, 0xf0, 1, {0xf00000}, "Bus BRIL Burst Transaction Results in HITM"}, */
	{155, 155, str_nil, "BUS_RD_INVAL_HITM"},	/* { "BUS_RD_INVAL_HITM", {0x82}, 0xf0, 1, {0xf00000}, "Bus BIL Transaction Results in HITM"}, */
	{156, 156, str_nil, "BUS_RD_IO_ANY"},	/* { "BUS_RD_IO_ANY", {0x30091}, 0xf0, 1, {0xf00000}, "IA-32 Compatible IO Read Transactions -- CPU or non-CPU (all transactions)."}, */
	{157, 157, str_nil, "BUS_RD_IO_IO"},	/* { "BUS_RD_IO_IO", {0x10091}, 0xf0, 1, {0xf00000}, "IA-32 Compatible IO Read Transactions -- non-CPU priority agents"}, */
	{158, 158, str_nil, "BUS_RD_IO_SELF"},	/* { "BUS_RD_IO_SELF", {0x20091}, 0xf0, 1, {0xf00000}, "IA-32 Compatible IO Read Transactions -- local processor"}, */
	{159, 159, str_nil, "BUS_RD_PRTL_ANY"},	/* { "BUS_RD_PRTL_ANY", {0x3008d}, 0xf0, 1, {0xf00000}, "Bus Read Partial Transactions -- CPU or non-CPU (all transactions)."}, */
	{160, 160, str_nil, "BUS_RD_PRTL_IO"},	/* { "BUS_RD_PRTL_IO", {0x1008d}, 0xf0, 1, {0xf00000}, "Bus Read Partial Transactions -- non-CPU priority agents"}, */
	{161, 161, str_nil, "BUS_RD_PRTL_SELF"},	/* { "BUS_RD_PRTL_SELF", {0x2008d}, 0xf0, 1, {0xf00000}, "Bus Read Partial Transactions -- local processor"}, */
	{162, 162, str_nil, "BUS_SNOOPQ_REQ"},	/* { "BUS_SNOOPQ_REQ", {0x96}, 0xf0, 7, {0xf00000}, "Bus Snoop Queue Requests"}, */
	{163, 163, str_nil, "BUS_SNOOPS_ANY"},	/* { "BUS_SNOOPS_ANY", {0x30086}, 0xf0, 1, {0xf00000}, "Bus Snoops Total -- CPU or non-CPU (all transactions)."}, */
	{164, 164, str_nil, "BUS_SNOOPS_IO"},	/* { "BUS_SNOOPS_IO", {0x10086}, 0xf0, 1, {0xf00000}, "Bus Snoops Total -- non-CPU priority agents"}, */
	{165, 165, str_nil, "BUS_SNOOPS_SELF"},	/* { "BUS_SNOOPS_SELF", {0x20086}, 0xf0, 1, {0xf00000}, "Bus Snoops Total -- local processor"}, */
	{166, 166, str_nil, "BUS_SNOOPS_HITM_ANY"},	/* { "BUS_SNOOPS_HITM_ANY", {0x30085}, 0xf0, 1, {0xf00000}, "Bus Snoops HIT Modified Cache Line -- CPU or non-CPU (all transactions)."}, */
	{167, 167, str_nil, "BUS_SNOOPS_HITM_SELF"},	/* { "BUS_SNOOPS_HITM_SELF", {0x20085}, 0xf0, 1, {0xf00000}, "Bus Snoops HIT Modified Cache Line -- local processor"}, */
	{168, 168, str_nil, "BUS_SNOOP_STALL_CYCLES_ANY"},	/* { "BUS_SNOOP_STALL_CYCLES_ANY", {0x3008f}, 0xf0, 1, {0xf00000}, "Bus Snoop Stall Cycles (from any agent) -- CPU or non-CPU (all transactions)."}, */
	{169, 169, str_nil, "BUS_SNOOP_STALL_CYCLES_SELF"},	/* { "BUS_SNOOP_STALL_CYCLES_SELF", {0x2008f}, 0xf0, 1, {0xf00000}, "Bus Snoop Stall Cycles (from any agent) -- local processor"}, */
	{170, 170, str_nil, "BUS_WR_WB_ALL_ANY"},	/* { "BUS_WR_WB_ALL_ANY", {0xf0092}, 0xf0, 1, {0xf00000}, "Bus Write Back Transactions -- CPU or non-CPU (all transactions)."}, */
	{171, 171, str_nil, "BUS_WR_WB_ALL_IO"},	/* { "BUS_WR_WB_ALL_IO", {0xd0092}, 0xf0, 1, {0xf00000}, "Bus Write Back Transactions -- non-CPU priority agents"}, */
	{172, 172, str_nil, "BUS_WR_WB_ALL_SELF"},	/* { "BUS_WR_WB_ALL_SELF", {0xe0092}, 0xf0, 1, {0xf00000}, "Bus Write Back Transactions -- local processor"}, */
	{173, 173, str_nil, "BUS_WR_WB_CCASTOUT_ANY"},	/* { "BUS_WR_WB_CCASTOUT_ANY", {0xb0092}, 0xf0, 1, {0xf00000}, "Bus Write Back Transactions -- CPU or non-CPU (all transactions)/Only 0-byte transactions with write back attribute (clean cast outs) will be counted"}, */
	{174, 174, str_nil, "BUS_WR_WB_CCASTOUT_SELF"},	/* { "BUS_WR_WB_CCASTOUT_SELF", {0xa0092}, 0xf0, 1, {0xf00000}, "Bus Write Back Transactions -- local processor/Only 0-byte transactions with write back attribute (clean cast outs) will be counted"}, */
	{175, 175, str_nil, "BUS_WR_WB_EQ_128BYTE_ANY"},	/* { "BUS_WR_WB_EQ_128BYTE_ANY", {0x70092}, 0xf0, 1, {0xf00000}, "Bus Write Back Transactions -- CPU or non-CPU (all transactions)./Only cache line transactions with write back or write coalesce attributes will be counted."}, */
	{176, 176, str_nil, "BUS_WR_WB_EQ_128BYTE_IO"},	/* { "BUS_WR_WB_EQ_128BYTE_IO", {0x50092}, 0xf0, 1, {0xf00000}, "Bus Write Back Transactions -- non-CPU priority agents/Only cache line transactions with write back or write coalesce attributes will be counted."}, */
	{177, 177, str_nil, "BUS_WR_WB_EQ_128BYTE_SELF"},	/* { "BUS_WR_WB_EQ_128BYTE_SELF", {0x60092}, 0xf0, 1, {0xf00000}, "Bus Write Back Transactions -- local processor/Only cache line transactions with write back or write coalesce attributes will be counted."}, */
	{178, 178, str_nil, "CPU_CPL_CHANGES"},	/* { "CPU_CPL_CHANGES", {0x13}, 0xf0, 1, {0xf00000}, "Privilege Level Changes"}, */
	{179, 179, "cycles", "CPU_CYCLES"},	/* { "CPU_CYCLES", {0x12}, 0xf0, 1, {0xf00000}, "CPU Cycles"}, */
	{180, 180, str_nil, "DATA_DEBUG_REGISTER_FAULT"},	/* { "DATA_DEBUG_REGISTER_FAULT", {0x52}, 0xf0, 1, {0xf00000}, "Fault Due to Data Debug Reg. Match to Load/Store Instruction"}, */
	{181, 181, str_nil, "DATA_DEBUG_REGISTER_MATCHES"},	/* { "DATA_DEBUG_REGISTER_MATCHES", {0xc6}, 0xf0, 1, {0xf00007}, "Data Debug Register Matches Data Address of Memory Reference."}, */
	{182, 182, str_nil, "DATA_EAR_ALAT"},	/* { "DATA_EAR_ALAT", {0x6c8}, 0xf0, 1, {0xf00007}, "Data EAR ALAT"}, */
	{183, 183, str_nil, "DATA_EAR_CACHE_LAT1024"},	/* { "DATA_EAR_CACHE_LAT1024", {0x805c8}, 0xf0, 1, {0xf00007}, "Data EAR Cache -- >= 1024 Cycles"}, */
	{184, 184, str_nil, "DATA_EAR_CACHE_LAT128"},	/* { "DATA_EAR_CACHE_LAT128", {0x505c8}, 0xf0, 1, {0xf00007}, "Data EAR Cache -- >= 128 Cycles"}, */
	{185, 185, str_nil, "DATA_EAR_CACHE_LAT16"},	/* { "DATA_EAR_CACHE_LAT16", {0x205c8}, 0xf0, 1, {0xf00007}, "Data EAR Cache -- >= 16 Cycles"}, */
	{186, 186, str_nil, "DATA_EAR_CACHE_LAT2048"},	/* { "DATA_EAR_CACHE_LAT2048", {0x905c8}, 0xf0, 1, {0xf00007}, "Data EAR Cache -- >= 2048 Cycles"}, */
	{187, 187, str_nil, "DATA_EAR_CACHE_LAT256"},	/* { "DATA_EAR_CACHE_LAT256", {0x605c8}, 0xf0, 1, {0xf00007}, "Data EAR Cache -- >= 256 Cycles"}, */
	{188, 188, str_nil, "DATA_EAR_CACHE_LAT32"},	/* { "DATA_EAR_CACHE_LAT32", {0x305c8}, 0xf0, 1, {0xf00007}, "Data EAR Cache -- >= 32 Cycles"}, */
	{189, 189, str_nil, "DATA_EAR_CACHE_LAT4"},	/* { "DATA_EAR_CACHE_LAT4", {0x5c8}, 0xf0, 1, {0xf00007}, "Data EAR Cache -- >= 4 Cycles"}, */
	{190, 190, str_nil, "DATA_EAR_CACHE_LAT4096"},	/* { "DATA_EAR_CACHE_LAT4096", {0xa05c8}, 0xf0, 1, {0xf00007}, "Data EAR Cache -- >= 4096 Cycles"}, */
	{191, 191, str_nil, "DATA_EAR_CACHE_LAT512"},	/* { "DATA_EAR_CACHE_LAT512", {0x705c8}, 0xf0, 1, {0xf00007}, "Data EAR Cache -- >= 512 Cycles"}, */
	{192, 192, str_nil, "DATA_EAR_CACHE_LAT64"},	/* { "DATA_EAR_CACHE_LAT64", {0x405c8}, 0xf0, 1, {0xf00007}, "Data EAR Cache -- >= 64 Cycles"}, */
	{193, 193, str_nil, "DATA_EAR_CACHE_LAT8"},	/* { "DATA_EAR_CACHE_LAT8", {0x105c8}, 0xf0, 1, {0xf00007}, "Data EAR Cache -- >= 8 Cycles"}, */
	{194, 194, str_nil, "DATA_EAR_EVENTS"},	/* { "DATA_EAR_EVENTS", {0xc8}, 0xf0, 1, {0xf00007}, "L1 Data Cache EAR Events"}, */
	{195, 195, "TLB_misses", "DATA_EAR_TLB_ALL"},	/* { "DATA_EAR_TLB_ALL", {0xe04c8}, 0xf0, 1, {0xf00007}, "Data EAR TLB -- All L1 DTLB Misses"}, */
	{196, 196, str_nil, "DATA_EAR_TLB_FAULT"},	/* { "DATA_EAR_TLB_FAULT", {0x804c8}, 0xf0, 1, {0xf00007}, "Data EAR TLB -- DTLB Misses which produce a software fault"}, */
	{197, 197, str_nil, "DATA_EAR_TLB_L2DTLB"},	/* { "DATA_EAR_TLB_L2DTLB", {0x204c8}, 0xf0, 1, {0xf00007}, "Data EAR TLB -- L1 DTLB Misses which hit L2 DTLB"}, */
	{198, 198, str_nil, "DATA_EAR_TLB_L2DTLB_OR_FAULT"},	/* { "DATA_EAR_TLB_L2DTLB_OR_FAULT", {0xa04c8}, 0xf0, 1, {0xf00007}, "Data EAR TLB -- L1 DTLB Misses which hit L2 DTLB or produce a software fault"}, */
	{199, 199, str_nil, "DATA_EAR_TLB_L2DTLB_OR_VHPT"},	/* { "DATA_EAR_TLB_L2DTLB_OR_VHPT", {0x604c8}, 0xf0, 1, {0xf00007}, "Data EAR TLB -- L1 DTLB Misses which hit L2 DTLB or VHPT"}, */
	{200, 200, str_nil, "DATA_EAR_TLB_VHPT"},	/* { "DATA_EAR_TLB_VHPT", {0x404c8}, 0xf0, 1, {0xf00007}, "Data EAR TLB -- L1 DTLB Misses which hit VHPT"}, */
	{201, 201, str_nil, "DATA_EAR_TLB_VHPT_OR_FAULT"},	/* { "DATA_EAR_TLB_VHPT_OR_FAULT", {0xc04c8}, 0xf0, 1, {0xf00007}, "Data EAR TLB -- L1 DTLB Misses which hit VHPT or produce a software fault"}, */
	{202, 202, str_nil, "DATA_REFERENCES_SET0"},	/* { "DATA_REFERENCES_SET0", {0xc3}, 0xf0, 4, {0x5010007}, "Data Memory References Issued to Memory Pipeline"}, */
	{203, 203, str_nil, "DATA_REFERENCES_SET1"},	/* { "DATA_REFERENCES_SET1", {0xc5}, 0xf0, 4, {0x5110007}, "Data Memory References Issued to Memory Pipeline"}, */
	{204, 204, str_nil, "DISP_STALLED"},	/* { "DISP_STALLED", {0x49}, 0xf0, 1, {0xf00000}, "Number of Cycles Dispersal Stalled"}, */
	{205, 205, str_nil, "DTLB_INSERTS_HPW"},	/* { "DTLB_INSERTS_HPW", {0xc9}, 0xf0, 4, {0xf00007}, "Hardware Page Walker Installs to DTLB"}, */
	{206, 206, str_nil, "DTLB_INSERTS_HPW_RETIRED"},	/* { "DTLB_INSERTS_HPW_RETIRED", {0x2c}, 0xf0, 4, {0xf00007}, "VHPT Entries Inserted into DTLB by the Hardware Page Walker"}, */
	{207, 207, str_nil, "ENCBR_MISPRED_DETAIL_ALL_ALL_PRED"},	/* { "ENCBR_MISPRED_DETAIL_ALL_ALL_PRED", {0x63}, 0xf0, 3, {0xf00003}, "Number of Encoded Branches Retired -- All encoded branches regardless of prediction result"}, */
	{208, 208, str_nil, "ENCBR_MISPRED_DETAIL_ALL_CORRECT_PRED"},	/* { "ENCBR_MISPRED_DETAIL_ALL_CORRECT_PRED", {0x10063}, 0xf0, 3, {0xf00003}, "Number of Encoded Branches Retired -- All encoded branches, correctly predicted branches (outcome and target)"}, */
	{209, 209, str_nil, "ENCBR_MISPRED_DETAIL_ALL_WRONG_PATH"},	/* { "ENCBR_MISPRED_DETAIL_ALL_WRONG_PATH", {0x20063}, 0xf0, 3, {0xf00003}, "Number of Encoded Branches Retired -- All encoded branches, mispredicted branches due to wrong branch direction"}, */
	{210, 210, str_nil, "ENCBR_MISPRED_DETAIL_ALL_WRONG_TARGET"},	/* { "ENCBR_MISPRED_DETAIL_ALL_WRONG_TARGET", {0x30063}, 0xf0, 3, {0xf00003}, "Number of Encoded Branches Retired -- All encoded branches, mispredicted branches due to wrong target for taken branches"}, */
	{211, 211, str_nil, "ENCBR_MISPRED_DETAIL_ALL2_ALL_PRED"},	/* { "ENCBR_MISPRED_DETAIL_ALL2_ALL_PRED", {0xc0063}, 0xf0, 3, {0xf00003}, "Number of Encoded Branches Retired -- Only non-return indirect branches, regardless of prediction result"}, */
	{212, 212, str_nil, "ENCBR_MISPRED_DETAIL_ALL2_CORRECT_PRED"},	/* { "ENCBR_MISPRED_DETAIL_ALL2_CORRECT_PRED", {0xd0063}, 0xf0, 3, {0xf00003}, "Number of Encoded Branches Retired -- Only non-return indirect branches, correctly predicted branches (outcome and target)"}, */
	{213, 213, str_nil, "ENCBR_MISPRED_DETAIL_ALL2_WRONG_PATH"},	/* { "ENCBR_MISPRED_DETAIL_ALL2_WRONG_PATH", {0xe0063}, 0xf0, 3, {0xf00003}, "Number of Encoded Branches Retired -- Only non-return indirect branches, mispredicted branches due to wrong branch direction"}, */
	{214, 214, str_nil, "ENCBR_MISPRED_DETAIL_ALL2_WRONG_TARGET"},	/* { "ENCBR_MISPRED_DETAIL_ALL2_WRONG_TARGET", {0xf0063}, 0xf0, 3, {0xf00003}, "Number of Encoded Branches Retired -- Only non-return indirect branches, mispredicted branches due to wrong target for taken branches"}, */
	{215, 215, str_nil, "ENCBR_MISPRED_DETAIL_OVERSUB_ALL_PRED"},	/* { "ENCBR_MISPRED_DETAIL_OVERSUB_ALL_PRED", {0x80063}, 0xf0, 3, {0xf00003}, "Number of Encoded Branches Retired -- Only return type branches, regardless of prediction result"}, */
	{216, 216, str_nil, "ENCBR_MISPRED_DETAIL_OVERSUB_CORRECT_PRED"},	/* { "ENCBR_MISPRED_DETAIL_OVERSUB_CORRECT_PRED", {0x90063}, 0xf0, 3, {0xf00003}, "Number of Encoded Branches Retired -- Only return type branches, correctly predicted branches (outcome and target)"}, */
	{217, 217, str_nil, "ENCBR_MISPRED_DETAIL_OVERSUB_WRONG_PATH"},	/* { "ENCBR_MISPRED_DETAIL_OVERSUB_WRONG_PATH", {0xa0063}, 0xf0, 3, {0xf00003}, "Number of Encoded Branches Retired -- Only return type branches, mispredicted branches due to wrong branch direction"}, */
	{218, 218, str_nil, "ENCBR_MISPRED_DETAIL_OVERSUB_WRONG_TARGET"},	/* { "ENCBR_MISPRED_DETAIL_OVERSUB_WRONG_TARGET", {0xb0063}, 0xf0, 3, {0xf00003}, "Number of Encoded Branches Retired -- Only return type branches, mispredicted branches due to wrong target for taken branches"}, */
	{219, 219, str_nil, "EXTERN_DP_PINS_0_TO_3_PIN0"},	/* { "EXTERN_DP_PINS_0_TO_3_PIN0", {0x1009e}, 0xf0, 1, {0xf00000}, "DP Pins 0-3 Asserted -- include pin0 assertion"}, */
	{220, 220, str_nil, "EXTERN_DP_PINS_0_TO_3_PIN1"},	/* { "EXTERN_DP_PINS_0_TO_3_PIN1", {0x2009e}, 0xf0, 1, {0xf00000}, "DP Pins 0-3 Asserted -- include pin1 assertion"}, */
	{221, 221, str_nil, "EXTERN_DP_PINS_0_TO_3_PIN2"},	/* { "EXTERN_DP_PINS_0_TO_3_PIN2", {0x4009e}, 0xf0, 1, {0xf00000}, "DP Pins 0-3 Asserted -- include pin2 assertion"}, */
	{222, 222, str_nil, "EXTERN_DP_PINS_0_TO_3_PIN3"},	/* { "EXTERN_DP_PINS_0_TO_3_PIN3", {0x8009e}, 0xf0, 1, {0xf00000}, "DP Pins 0-3 Asserted -- include pin3 assertion"}, */
	{223, 223, str_nil, "EXTERN_DP_PINS_4_TO_5_PIN4"},	/* { "EXTERN_DP_PINS_4_TO_5_PIN4", {0x1009f}, 0xf0, 1, {0xf00000}, "DP Pins 4-5 Asserted -- include pin4 assertion"}, */
	{224, 224, str_nil, "EXTERN_DP_PINS_4_TO_5_PIN5"},	/* { "EXTERN_DP_PINS_4_TO_5_PIN5", {0x2009f}, 0xf0, 1, {0xf00000}, "DP Pins 4-5 Asserted -- include pin5 assertion"}, */
	{225, 225, str_nil, "FE_BUBBLE_ALL"},	/* { "FE_BUBBLE_ALL", {0x71}, 0xf0, 1, {0xf00000}, "Bubbles Seen by FE -- count regardless of cause"}, */
	{226, 226, str_nil, "FE_BUBBLE_ALLBUT_FEFLUSH_BUBBLE"},	/* { "FE_BUBBLE_ALLBUT_FEFLUSH_BUBBLE", {0xb0071}, 0xf0, 1, {0xf00000}, "Bubbles Seen by FE -- ALL except FEFLUSH and BUBBLE"}, */
	{227, 227, str_nil, "FE_BUBBLE_ALLBUT_IBFULL"},	/* { "FE_BUBBLE_ALLBUT_IBFULL", {0xc0071}, 0xf0, 1, {0xf00000}, "Bubbles Seen by FE -- ALL except IBFULl"}, */
	{228, 228, str_nil, "FE_BUBBLE_BRANCH"},	/* { "FE_BUBBLE_BRANCH", {0x90071}, 0xf0, 1, {0xf00000}, "Bubbles Seen by FE -- only if caused by any of 4 branch recirculates"}, */
	{229, 229, str_nil, "FE_BUBBLE_BUBBLE"},	/* { "FE_BUBBLE_BUBBLE", {0xd0071}, 0xf0, 1, {0xf00000}, "Bubbles Seen by FE -- only if caused by branch bubble stall"}, */
	{230, 230, str_nil, "FE_BUBBLE_FEFLUSH"},	/* { "FE_BUBBLE_FEFLUSH", {0x10071}, 0xf0, 1, {0xf00000}, "Bubbles Seen by FE -- only if caused by a front-end flush"}, */
	{231, 231, str_nil, "FE_BUBBLE_FILL_RECIRC"},	/* { "FE_BUBBLE_FILL_RECIRC", {0x80071}, 0xf0, 1, {0xf00000}, "Bubbles Seen by FE -- only if caused by a recirculate for a cache line fill operation"}, */
	{232, 232, str_nil, "FE_BUBBLE_GROUP1"},	/* { "FE_BUBBLE_GROUP1", {0x30071}, 0xf0, 1, {0xf00000}, "Bubbles Seen by FE -- BUBBLE or BRANCH"}, */
	{233, 233, str_nil, "FE_BUBBLE_GROUP2"},	/* { "FE_BUBBLE_GROUP2", {0x40071}, 0xf0, 1, {0xf00000}, "Bubbles Seen by FE -- IMISS or TLBMISS"}, */
	{234, 234, str_nil, "FE_BUBBLE_GROUP3"},	/* { "FE_BUBBLE_GROUP3", {0xa0071}, 0xf0, 1, {0xf00000}, "Bubbles Seen by FE -- FILL_RECIRC or BRANCH"}, */
	{235, 235, str_nil, "FE_BUBBLE_IBFULL"},	/* { "FE_BUBBLE_IBFULL", {0x50071}, 0xf0, 1, {0xf00000}, "Bubbles Seen by FE -- only if caused by instruction buffer full stall"}, */
	{236, 236, str_nil, "FE_BUBBLE_IMISS"},	/* { "FE_BUBBLE_IMISS", {0x60071}, 0xf0, 1, {0xf00000}, "Bubbles Seen by FE -- only if caused by instruction cache miss stall"}, */
	{237, 237, str_nil, "FE_BUBBLE_TLBMISS"},	/* { "FE_BUBBLE_TLBMISS", {0x70071}, 0xf0, 1, {0xf00000}, "Bubbles Seen by FE -- only if caused by TLB stall"}, */
	{238, 238, str_nil, "FE_LOST_BW_ALL"},	/* { "FE_LOST_BW_ALL", {0x70}, 0xf0, 2, {0xf00000}, "Invalid Bundles at the Entrance to IB -- count regardless of cause"}, */
	{239, 239, str_nil, "FE_LOST_BW_BI"},	/* { "FE_LOST_BW_BI", {0x90070}, 0xf0, 2, {0xf00000}, "Invalid Bundles at the Entrance to IB -- only if caused by branch initialization stall"}, */
	{240, 240, str_nil, "FE_LOST_BW_BRQ"},	/* { "FE_LOST_BW_BRQ", {0xa0070}, 0xf0, 2, {0xf00000}, "Invalid Bundles at the Entrance to IB -- only if caused by branch retirement queue stall"}, */
	{241, 241, str_nil, "FE_LOST_BW_BR_ILOCK"},	/* { "FE_LOST_BW_BR_ILOCK", {0xc0070}, 0xf0, 2, {0xf00000}, "Invalid Bundles at the Entrance to IB -- only if caused by branch interlock stall"}, */
	{242, 242, str_nil, "FE_LOST_BW_BUBBLE"},	/* { "FE_LOST_BW_BUBBLE", {0xd0070}, 0xf0, 2, {0xf00000}, "Invalid Bundles at the Entrance to IB -- only if caused by branch resteer bubble stall"}, */
	{243, 243, str_nil, "FE_LOST_BW_FEFLUSH"},	/* { "FE_LOST_BW_FEFLUSH", {0x10070}, 0xf0, 2, {0xf00000}, "Invalid Bundles at the Entrance to IB -- only if caused by a front-end flush"}, */
	{244, 244, str_nil, "FE_LOST_BW_FILL_RECIRC"},	/* { "FE_LOST_BW_FILL_RECIRC", {0x80070}, 0xf0, 2, {0xf00000}, "Invalid Bundles at the Entrance to IB -- only if caused by a recirculate for a cache line fill operation"}, */
	{245, 245, str_nil, "FE_LOST_BW_IBFULL"},	/* { "FE_LOST_BW_IBFULL", {0x50070}, 0xf0, 2, {0xf00000}, "Invalid Bundles at the Entrance to IB -- only if caused by instruction buffer full stall"}, */
	{246, 246, str_nil, "FE_LOST_BW_IMISS"},	/* { "FE_LOST_BW_IMISS", {0x60070}, 0xf0, 2, {0xf00000}, "Invalid Bundles at the Entrance to IB -- only if caused by instruction cache miss stall"}, */
	{247, 247, str_nil, "FE_LOST_BW_PLP"},	/* { "FE_LOST_BW_PLP", {0xb0070}, 0xf0, 2, {0xf00000}, "Invalid Bundles at the Entrance to IB -- only if caused by perfect loop prediction stall"}, */
	{248, 248, str_nil, "FE_LOST_BW_TLBMISS"},	/* { "FE_LOST_BW_TLBMISS", {0x70070}, 0xf0, 2, {0xf00000}, "Invalid Bundles at the Entrance to IB -- only if caused by TLB stall"}, */
	{249, 249, str_nil, "FE_LOST_BW_UNREACHED"},	/* { "FE_LOST_BW_UNREACHED", {0x40070}, 0xf0, 2, {0xf00000}, "Invalid Bundles at the Entrance to IB -- only if caused by unreachable bundle"}, */
	{250, 250, str_nil, "FP_FAILED_FCHKF"},	/* { "FP_FAILED_FCHKF", {0x6}, 0xf0, 1, {0xf00001}, "Failed fchkf"}, */
	{251, 251, str_nil, "FP_FALSE_SIRSTALL"},	/* { "FP_FALSE_SIRSTALL", {0x5}, 0xf0, 1, {0xf00001}, "SIR Stall Without a Trap"}, */
	{252, 252, str_nil, "FP_FLUSH_TO_ZERO"},	/* { "FP_FLUSH_TO_ZERO", {0xb}, 0xf0, 2, {0xf00001}, "FP Result Flushed to Zero"}, */
	{253, 253, str_nil, "FP_OPS_RETIRED"},	/* { "FP_OPS_RETIRED", {0x9}, 0xf0, 4, {0xf00001}, "Retired FP Operations"}, */
	{254, 254, str_nil, "FP_TRUE_SIRSTALL"},	/* { "FP_TRUE_SIRSTALL", {0x3}, 0xf0, 1, {0xf00001}, "SIR stall asserted and leads to a trap"}, */
	{255, 255, str_nil, "HPW_DATA_REFERENCES"},	/* { "HPW_DATA_REFERENCES", {0x2d}, 0xf0, 4, {0xf00007}, "Data Memory References to VHPT"}, */
	{256, 256, str_nil, "IA32_INST_RETIRED"},	/* { "IA32_INST_RETIRED", {0x59}, 0xf0, 2, {0xf00000}, "IA-32 Instructions Retired"}, */
	{257, 257, str_nil, "IA32_ISA_TRANSITIONS"},	/* { "IA32_ISA_TRANSITIONS", {0x7}, 0xf0, 1, {0xf00000}, "IA-64 to/from IA-32 ISA Transitions"}, */
	{258, 258, str_nil, "IA64_INST_RETIRED"},	/* { "IA64_INST_RETIRED", {0x8}, 0xf0, 6, {0xf00003}, "Retired IA-64 Instructions, alias to IA64_INST_RETIRED_THIS"}, */
	{259, 259, str_nil, "IA64_INST_RETIRED_THIS"},	/* { "IA64_INST_RETIRED_THIS", {0x8}, 0xf0, 6, {0xf00003}, "Retired IA-64 Instructions -- Retired IA-64 Instructions"}, */
	{260, 260, str_nil, "IA64_TAGGED_INST_RETIRED_IBRP0_PMC8"},	/* { "IA64_TAGGED_INST_RETIRED_IBRP0_PMC8", {0x8}, 0xf0, 6, {0xf00003}, "Retired Tagged Instructions -- Instruction tagged by Instruction Breakpoint Pair 0 and opcode matcher PMC8. Code executed with PSR.is=1 is included."}, */
	{261, 261, str_nil, "IA64_TAGGED_INST_RETIRED_IBRP1_PMC9"},	/* { "IA64_TAGGED_INST_RETIRED_IBRP1_PMC9", {0x10008}, 0xf0, 6, {0xf00003}, "Retired Tagged Instructions -- Instruction tagged by Instruction Breakpoint Pair 1 and opcode matcher PMC9. Code executed with PSR.is=1 is included."}, */
	{262, 262, str_nil, "IA64_TAGGED_INST_RETIRED_IBRP2_PMC8"},	/* { "IA64_TAGGED_INST_RETIRED_IBRP2_PMC8", {0x20008}, 0xf0, 6, {0xf00003}, "Retired Tagged Instructions -- Instruction tagged by Instruction Breakpoint Pair 2 and opcode matcher PMC8. Code executed with PSR.is=1 is not included."}, */
	{263, 263, str_nil, "IA64_TAGGED_INST_RETIRED_IBRP3_PMC9"},	/* { "IA64_TAGGED_INST_RETIRED_IBRP3_PMC9", {0x30008}, 0xf0, 6, {0xf00003}, "Retired Tagged Instructions -- Instruction tagged by Instruction Breakpoint Pair 3 and opcode matcher PMC9. Code executed with PSR.is=1 is not included."}, */
	{264, 264, str_nil, "IDEAL_BE_LOST_BW_DUE_TO_FE_ALL"},	/* { "IDEAL_BE_LOST_BW_DUE_TO_FE_ALL", {0x73}, 0xf0, 2, {0xf00000}, "Invalid Bundles at the Exit from IB -- count regardless of cause"}, */
	{265, 265, str_nil, "IDEAL_BE_LOST_BW_DUE_TO_FE_BI"},	/* { "IDEAL_BE_LOST_BW_DUE_TO_FE_BI", {0x90073}, 0xf0, 2, {0xf00000}, "Invalid Bundles at the Exit from IB -- only if caused by branch initialization stall"}, */
	{266, 266, str_nil, "IDEAL_BE_LOST_BW_DUE_TO_FE_BRQ"},	/* { "IDEAL_BE_LOST_BW_DUE_TO_FE_BRQ", {0xa0073}, 0xf0, 2, {0xf00000}, "Invalid Bundles at the Exit from IB -- only if caused by branch retirement queue stall"}, */
	{267, 267, str_nil, "IDEAL_BE_LOST_BW_DUE_TO_FE_BR_ILOCK"},	/* { "IDEAL_BE_LOST_BW_DUE_TO_FE_BR_ILOCK", {0xc0073}, 0xf0, 2, {0xf00000}, "Invalid Bundles at the Exit from IB -- only if caused by branch interlock stall"}, */
	{268, 268, str_nil, "IDEAL_BE_LOST_BW_DUE_TO_FE_BUBBLE"},	/* { "IDEAL_BE_LOST_BW_DUE_TO_FE_BUBBLE", {0xd0073}, 0xf0, 2, {0xf00000}, "Invalid Bundles at the Exit from IB -- only if caused by branch resteer bubble stall"}, */
	{269, 269, str_nil, "IDEAL_BE_LOST_BW_DUE_TO_FE_FEFLUSH"},	/* { "IDEAL_BE_LOST_BW_DUE_TO_FE_FEFLUSH", {0x10073}, 0xf0, 2, {0xf00000}, "Invalid Bundles at the Exit from IB -- only if caused by a front-end flush"}, */
	{270, 270, str_nil, "IDEAL_BE_LOST_BW_DUE_TO_FE_FILL_RECIRC"},	/* { "IDEAL_BE_LOST_BW_DUE_TO_FE_FILL_RECIRC", {0x80073}, 0xf0, 2, {0xf00000}, "Invalid Bundles at the Exit from IB -- only if caused by a recirculate for a cache line fill operation"}, */
	{271, 271, str_nil, "IDEAL_BE_LOST_BW_DUE_TO_FE_IBFULL"},	/* { "IDEAL_BE_LOST_BW_DUE_TO_FE_IBFULL", {0x50073}, 0xf0, 2, {0xf00000}, "Invalid Bundles at the Exit from IB -- (* meaningless for this event *)"}, */
	{272, 272, str_nil, "IDEAL_BE_LOST_BW_DUE_TO_FE_IMISS"},	/* { "IDEAL_BE_LOST_BW_DUE_TO_FE_IMISS", {0x60073}, 0xf0, 2, {0xf00000}, "Invalid Bundles at the Exit from IB -- only if caused by instruction cache miss stall"}, */
	{273, 273, str_nil, "IDEAL_BE_LOST_BW_DUE_TO_FE_PLP"},	/* { "IDEAL_BE_LOST_BW_DUE_TO_FE_PLP", {0xb0073}, 0xf0, 2, {0xf00000}, "Invalid Bundles at the Exit from IB -- only if caused by perfect loop prediction stall"}, */
	{274, 274, str_nil, "IDEAL_BE_LOST_BW_DUE_TO_FE_TLBMISS"},	/* { "IDEAL_BE_LOST_BW_DUE_TO_FE_TLBMISS", {0x70073}, 0xf0, 2, {0xf00000}, "Invalid Bundles at the Exit from IB -- only if caused by TLB stall"}, */
	{275, 275, str_nil, "IDEAL_BE_LOST_BW_DUE_TO_FE_UNREACHED"},	/* { "IDEAL_BE_LOST_BW_DUE_TO_FE_UNREACHED", {0x40073}, 0xf0, 2, {0xf00000}, "Invalid Bundles at the Exit from IB -- only if caused by unreachable bundle"}, */
	{276, 276, str_nil, "INST_CHKA_LDC_ALAT_ALL"},	/* { "INST_CHKA_LDC_ALAT_ALL", {0x30056}, 0xf0, 2, {0xf00007}, "Retired chk.a and ld.c Instructions -- both integer and floating point instructions"}, */
	{277, 277, str_nil, "INST_CHKA_LDC_ALAT_FP"},	/* { "INST_CHKA_LDC_ALAT_FP", {0x20056}, 0xf0, 2, {0xf00007}, "Retired chk.a and ld.c Instructions -- only floating point instructions"}, */
	{278, 278, str_nil, "INST_CHKA_LDC_ALAT_INT"},	/* { "INST_CHKA_LDC_ALAT_INT", {0x10056}, 0xf0, 2, {0xf00007}, "Retired chk.a and ld.c Instructions -- only integer instructions"}, */
	{279, 279, str_nil, "INST_DISPERSED"},	/* { "INST_DISPERSED", {0x4d}, 0xf0, 6, {0xf00001}, "Syllables Dispersed from REN to REG stage"}, */
	{280, 280, str_nil, "INST_FAILED_CHKA_LDC_ALAT_ALL"},	/* { "INST_FAILED_CHKA_LDC_ALAT_ALL", {0x30057}, 0xf0, 1, {0xf00007}, "Failed chk.a and ld.c Instructions -- both integer and floating point instructions"}, */
	{281, 281, str_nil, "INST_FAILED_CHKA_LDC_ALAT_FP"},	/* { "INST_FAILED_CHKA_LDC_ALAT_FP", {0x20057}, 0xf0, 1, {0xf00007}, "Failed chk.a and ld.c Instructions -- only floating point instructions"}, */
	{282, 282, str_nil, "INST_FAILED_CHKA_LDC_ALAT_INT"},	/* { "INST_FAILED_CHKA_LDC_ALAT_INT", {0x10057}, 0xf0, 1, {0xf00007}, "Failed chk.a and ld.c Instructions -- only integer instructions"}, */
	{283, 283, str_nil, "INST_FAILED_CHKS_RETIRED_ALL"},	/* { "INST_FAILED_CHKS_RETIRED_ALL", {0x30055}, 0xf0, 1, {0xf00000}, "Failed chk.s Instructions -- both integer and floating point instructions"}, */
	{284, 284, str_nil, "INST_FAILED_CHKS_RETIRED_FP"},	/* { "INST_FAILED_CHKS_RETIRED_FP", {0x20055}, 0xf0, 1, {0xf00000}, "Failed chk.s Instructions -- only floating point instructions"}, */
	{285, 285, str_nil, "INST_FAILED_CHKS_RETIRED_INT"},	/* { "INST_FAILED_CHKS_RETIRED_INT", {0x10055}, 0xf0, 1, {0xf00000}, "Failed chk.s Instructions -- only integer instructions"}, */
	{286, 286, str_nil, "ISB_BUNPAIRS_IN"},	/* { "ISB_BUNPAIRS_IN", {0x46}, 0xf0, 1, {0xf00001}, "Bundle Pairs Written from L2 into FE"}, */
	{287, 287, "iTLB_misses", "ITLB_MISSES_FETCH_ALL"},	/* { "ITLB_MISSES_FETCH_ALL", {0x30047}, 0xf0, 1, {0xf00001}, "ITLB Misses Demand Fetch -- All tlb misses will be counted. Note that this is not equal to sum of the L1ITLB and L2ITLB umasks because any access could be a miss in L1ITLB and L2ITLB."}, */
	{288, 288, str_nil, "ITLB_MISSES_FETCH_L1ITLB"},	/* { "ITLB_MISSES_FETCH_L1ITLB", {0x10047}, 0xf0, 1, {0xf00001}, "ITLB Misses Demand Fetch -- All misses in L1ITLB will be counted. even if L1ITLB is not updated for an access (Uncacheable/nat page/not present page/faulting/some flushed), it will be counted here."}, */
	{289, 289, str_nil, "ITLB_MISSES_FETCH_L2ITLB"},	/* { "ITLB_MISSES_FETCH_L2ITLB", {0x20047}, 0xf0, 1, {0xf00001}, "ITLB Misses Demand Fetch -- All misses in L1ITLB which also missed in L2ITLB will be counted."}, */
	{290, 290, str_nil, "L1DTLB_TRANSFER"},	/* { "L1DTLB_TRANSFER", {0xc0}, 0xf0, 1, {0x5010007}, "L1DTLB Misses That Hit in the L2DTLB for Accesses Counted in L1D_READS"}, */
	{291, 291, str_nil, "L1D_READS_SET0"},	/* { "L1D_READS_SET0", {0xc2}, 0xf0, 2, {0x5010007}, "L1 Data Cache Reads"}, */
	{292, 292, str_nil, "L1D_READS_SET1"},	/* { "L1D_READS_SET1", {0xc4}, 0xf0, 2, {0x5110007}, "L1 Data Cache Reads"}, */
	{293, 293, "L1_data_misses", "L1D_READ_MISSES_ALL"},	/* { "L1D_READ_MISSES_ALL", {0xc7}, 0xf0, 2, {0x5110007}, "L1 Data Cache Read Misses -- all L1D read misses will be counted."}, */
	{294, 294, str_nil, "L1D_READ_MISSES_RSE_FILL"},	/* { "L1D_READ_MISSES_RSE_FILL", {0x100c7}, 0xf0, 2, {0x5110007}, "L1 Data Cache Read Misses -- only L1D read misses caused by RSE fills will be counted"}, */
	{295, 295, str_nil, "L1ITLB_INSERTS_HPW"},	/* { "L1ITLB_INSERTS_HPW", {0x48}, 0xf0, 1, {0xf00001}, "L1ITLB Hardware Page Walker Inserts"}, */
	{296, 296, "L1_inst_misses", "L1I_EAR_CACHE_LAT0"},	/* { "L1I_EAR_CACHE_LAT0", {0x400343}, 0xf0, 1, {0xf00001}, "L1I EAR Cache -- > 0 Cycles (All L1 Misses)"}, */
	{297, 297, str_nil, "L1I_EAR_CACHE_LAT1024"},	/* { "L1I_EAR_CACHE_LAT1024", {0xc00343}, 0xf0, 1, {0xf00001}, "L1I EAR Cache -- >= 1024 Cycles"}, */
	{298, 298, str_nil, "L1I_EAR_CACHE_LAT128"},	/* { "L1I_EAR_CACHE_LAT128", {0xf00343}, 0xf0, 1, {0xf00001}, "L1I EAR Cache -- >= 128 Cycles"}, */
	{299, 299, str_nil, "L1I_EAR_CACHE_LAT16"},	/* { "L1I_EAR_CACHE_LAT16", {0xfc0343}, 0xf0, 1, {0xf00001}, "L1I EAR Cache -- >= 16 Cycles"}, */
	{300, 300, str_nil, "L1I_EAR_CACHE_LAT256"},	/* { "L1I_EAR_CACHE_LAT256", {0xe00343}, 0xf0, 1, {0xf00001}, "L1I EAR Cache -- >= 256 Cycles"}, */
	{301, 301, str_nil, "L1I_EAR_CACHE_LAT32"},	/* { "L1I_EAR_CACHE_LAT32", {0xf80343}, 0xf0, 1, {0xf00001}, "L1I EAR Cache -- >= 32 Cycles"}, */
	{302, 302, str_nil, "L1I_EAR_CACHE_LAT4"},	/* { "L1I_EAR_CACHE_LAT4", {0xff0343}, 0xf0, 1, {0xf00001}, "L1I EAR Cache -- >= 4 Cycles"}, */
	{303, 303, str_nil, "L1I_EAR_CACHE_LAT4096"},	/* { "L1I_EAR_CACHE_LAT4096", {0x800343}, 0xf0, 1, {0xf00001}, "L1I EAR Cache -- >= 4096 Cycles"}, */
	{304, 304, str_nil, "L1I_EAR_CACHE_LAT8"},	/* { "L1I_EAR_CACHE_LAT8", {0xfe0343}, 0xf0, 1, {0xf00001}, "L1I EAR Cache -- >= 8 Cycles"}, */
	{305, 305, str_nil, "L1I_EAR_CACHE_RAB"},	/* { "L1I_EAR_CACHE_RAB", {0x343}, 0xf0, 1, {0xf00001}, "L1I EAR Cache -- RAB HIT"}, */
	{306, 306, str_nil, "L1I_EAR_EVENTS"},	/* { "L1I_EAR_EVENTS", {0x43}, 0xf0, 1, {0xf00001}, "Instruction EAR Events"}, */
	{307, 307, str_nil, "L1I_EAR_TLB_ALL"},	/* { "L1I_EAR_TLB_ALL", {0x70243}, 0xf0, 1, {0xf00001}, "L1I EAR TLB -- All L1 ITLB Misses"}, */
	{308, 308, str_nil, "L1I_EAR_TLB_FAULT"},	/* { "L1I_EAR_TLB_FAULT", {0x40243}, 0xf0, 1, {0xf00001}, "L1I EAR TLB -- ITLB Misses which produced a fault"}, */
	{309, 309, str_nil, "L1I_EAR_TLB_L2TLB"},	/* { "L1I_EAR_TLB_L2TLB", {0x10243}, 0xf0, 1, {0xf00001}, "L1I EAR TLB -- L1 ITLB Misses which hit L2 ITLB"}, */
	{310, 310, str_nil, "L1I_EAR_TLB_L2TLB_OR_FAULT"},	/* { "L1I_EAR_TLB_L2TLB_OR_FAULT", {0x50243}, 0xf0, 1, {0xf00001}, "L1I EAR TLB -- L1 ITLB Misses which hit L2 ITLB or produce a software fault"}, */
	{311, 311, str_nil, "L1I_EAR_TLB_L2TLB_OR_VHPT"},	/* { "L1I_EAR_TLB_L2TLB_OR_VHPT", {0x30243}, 0xf0, 1, {0xf00001}, "L1I EAR TLB -- L1 ITLB Misses which hit L2 ITLB or VHPT"}, */
	{312, 312, str_nil, "L1I_EAR_TLB_VHPT"},	/* { "L1I_EAR_TLB_VHPT", {0x20243}, 0xf0, 1, {0xf00001}, "L1I EAR TLB -- L1 ITLB Misses which hit VHPT"}, */
	{313, 313, str_nil, "L1I_EAR_TLB_VHPT_OR_FAULT"},	/* { "L1I_EAR_TLB_VHPT_OR_FAULT", {0x60243}, 0xf0, 1, {0xf00001}, "L1I EAR TLB -- L1 ITLB Misses which hit VHPT or produce a software fault"}, */
	{314, 314, str_nil, "L1I_FETCH_ISB_HIT"},	/* { "L1I_FETCH_ISB_HIT", {0x66}, 0xf0, 1, {0xf00001}, "\"Just-In-Time\" Instruction Fetch Hitting in and Being Bypassed from ISB"}, */
	{315, 315, str_nil, "L1I_FETCH_RAB_HIT"},	/* { "L1I_FETCH_RAB_HIT", {0x65}, 0xf0, 1, {0xf00001}, "Instruction Fetch Hitting in RAB"}, */
	{316, 316, str_nil, "L1I_FILLS"},	/* { "L1I_FILLS", {0x41}, 0xf0, 1, {0xf00001}, "L1 Instruction Cache Fills"}, */
	{317, 317, str_nil, "L1I_PREFETCHES"},	/* { "L1I_PREFETCHES", {0x44}, 0xf0, 1, {0xf00001}, "L1 Instruction Prefetch Requests"}, */
	{318, 318, str_nil, "L1I_PREFETCH_STALL_ALL"},	/* { "L1I_PREFETCH_STALL_ALL", {0x30067}, 0xf0, 1, {0xf00000}, "Prefetch Pipeline Stalls -- Number of clocks prefetch pipeline is stalled"}, */
	{319, 319, str_nil, "L1I_PREFETCH_STALL_FLOW"},	/* { "L1I_PREFETCH_STALL_FLOW", {0x20067}, 0xf0, 1, {0xf00000}, "Prefetch Pipeline Stalls -- Number of clocks flow is not asserted"}, */
	{320, 320, str_nil, "L1I_PURGE"},	/* { "L1I_PURGE", {0x4b}, 0xf0, 1, {0xf00001}, "L1ITLB Purges Handled by L1I"}, */
	{321, 321, str_nil, "L1I_PVAB_OVERFLOW"},	/* { "L1I_PVAB_OVERFLOW", {0x69}, 0xf0, 1, {0xf00000}, "PVAB Overflow"}, */
	{322, 322, str_nil, "L1I_RAB_ALMOST_FULL"},	/* { "L1I_RAB_ALMOST_FULL", {0x64}, 0xf0, 1, {0xf00000}, "Is RAB Almost Full?"}, */
	{323, 323, str_nil, "L1I_RAB_FULL"},	/* { "L1I_RAB_FULL", {0x60}, 0xf0, 1, {0xf00000}, "Is RAB Full?"}, */
	{324, 324, str_nil, "L1I_READS"},	/* { "L1I_READS", {0x40}, 0xf0, 1, {0xf00001}, "L1 Instruction Cache Reads"}, */
	{325, 325, str_nil, "L1I_SNOOP"},	/* { "L1I_SNOOP", {0x4a}, 0xf0, 1, {0xf00007}, "Snoop Requests Handled by L1I"}, */
	{326, 326, str_nil, "L1I_STRM_PREFETCHES"},	/* { "L1I_STRM_PREFETCHES", {0x5f}, 0xf0, 1, {0xf00001}, "L1 Instruction Cache Line Prefetch Requests"}, */
	{327, 327, str_nil, "L2DTLB_MISSES"},	/* { "L2DTLB_MISSES", {0xc1}, 0xf0, 4, {0x5010007}, "L2DTLB Misses"}, */
	{328, 328, str_nil, "L2_BAD_LINES_SELECTED_ANY"},	/* { "L2_BAD_LINES_SELECTED_ANY", {0xb9}, 0xf0, 4, {0x4320007}, "Valid Line Replaced When Invalid Line Is Available -- Valid line replaced when invalid line is available"}, */
	{329, 329, str_nil, "L2_BYPASS_L2_DATA1"},	/* { "L2_BYPASS_L2_DATA1", {0xb8}, 0xf0, 1, {0x4320007}, "Count L2 Bypasses -- Count only L2 data bypasses (L1D to L2A)"}, */
	{330, 330, str_nil, "L2_BYPASS_L2_DATA2"},	/* { "L2_BYPASS_L2_DATA2", {0x100b8}, 0xf0, 1, {0x4320007}, "Count L2 Bypasses -- Count only L2 data bypasses (L1W to L2I)"}, */
	{331, 331, str_nil, "L2_BYPASS_L2_INST1"},	/* { "L2_BYPASS_L2_INST1", {0x400b8}, 0xf0, 1, {0x4320007}, "Count L2 Bypasses -- Count only L2 instruction bypasses (L1D to L2A)"}, */
	{332, 332, str_nil, "L2_BYPASS_L2_INST2"},	/* { "L2_BYPASS_L2_INST2", {0x500b8}, 0xf0, 1, {0x4320007}, "Count L2 Bypasses -- Count only L2 instruction bypasses (L1W to L2I)"}, */
	{333, 333, str_nil, "L2_BYPASS_L3_DATA1"},	/* { "L2_BYPASS_L3_DATA1", {0x200b8}, 0xf0, 1, {0x4320007}, "Count L2 Bypasses -- Count only L3 data bypasses (L1D to L2A)"}, */
	{334, 334, str_nil, "L2_BYPASS_L3_INST1"},	/* { "L2_BYPASS_L3_INST1", {0x600b8}, 0xf0, 1, {0x4320007}, "Count L2 Bypasses -- Count only L3 instruction bypasses (L1D to L2A)"}, */
	{335, 335, str_nil, "L2_DATA_REFERENCES_L2_ALL"},	/* { "L2_DATA_REFERENCES_L2_ALL", {0x300b2}, 0xf0, 4, {0x4120007}, "Data Read/Write Access to L2 -- count both read and write operations (semaphores will count as 2)"}, */
	{336, 336, str_nil, "L2_DATA_REFERENCES_L2_DATA_READS"},	/* { "L2_DATA_REFERENCES_L2_DATA_READS", {0x100b2}, 0xf0, 4, {0x4120007}, "Data Read/Write Access to L2 -- count only data read and semaphore operations."}, */
	{337, 337, str_nil, "L2_DATA_REFERENCES_L2_DATA_WRITES"},	/* { "L2_DATA_REFERENCES_L2_DATA_WRITES", {0x200b2}, 0xf0, 4, {0x4120007}, "Data Read/Write Access to L2 -- count only data write and semaphore operations"}, */
	{338, 338, str_nil, "L2_FILLB_FULL_THIS"},	/* { "L2_FILLB_FULL_THIS", {0xbf}, 0xf0, 1, {0x4520000}, "L2D Fill Buffer Is Full -- L2 Fill buffer is full"}, */
	{339, 339, str_nil, "L2_FORCE_RECIRC_ANY"},	/* { "L2_FORCE_RECIRC_ANY", {0xb4}, 0x10, 4, {0x4220007}, "Forced Recirculates -- count forced recirculates regardless of cause. SMC_HIT, TRAN_PREF & SNP_OR_L3 will not be included here."}, */
	{340, 340, str_nil, "L2_FORCE_RECIRC_FILL_HIT"},	/* { "L2_FORCE_RECIRC_FILL_HIT", {0x900b4}, 0x10, 4, {0x4220007}, "Forced Recirculates -- count only those caused by an L2 miss which hit in the fill buffer."}, */
	{341, 341, str_nil, "L2_FORCE_RECIRC_FRC_RECIRC"},	/* { "L2_FORCE_RECIRC_FRC_RECIRC", {0xe00b4}, 0x10, 4, {0x4220007}, "Forced Recirculates -- caused by an L2 miss when a force recirculate already existed"}, */
	{342, 342, str_nil, "L2_FORCE_RECIRC_IPF_MISS"},	/* { "L2_FORCE_RECIRC_IPF_MISS", {0xa00b4}, 0x10, 4, {0x4220007}, "Forced Recirculates -- caused by L2 miss when instruction prefetch buffer miss already existed"}, */
	{343, 343, str_nil, "L2_FORCE_RECIRC_L1W"},	/* { "L2_FORCE_RECIRC_L1W", {0x200b4}, 0x10, 4, {0x4220007}, "Forced Recirculates -- count only those caused by forced limbo"}, */
	{344, 344, str_nil, "L2_FORCE_RECIRC_OZQ_MISS"},	/* { "L2_FORCE_RECIRC_OZQ_MISS", {0xc00b4}, 0x10, 4, {0x4220007}, "Forced Recirculates -- caused by an L2 miss when an OZQ miss already existed"}, */
	{345, 345, str_nil, "L2_FORCE_RECIRC_SAME_INDEX"},	/* { "L2_FORCE_RECIRC_SAME_INDEX", {0xd00b4}, 0x10, 4, {0x4220007}, "Forced Recirculates -- caused by an L2 miss when a miss to the same index already existed"}, */
	{346, 346, str_nil, "L2_FORCE_RECIRC_SMC_HIT"},	/* { "L2_FORCE_RECIRC_SMC_HIT", {0x100b4}, 0x10, 4, {0x4220007}, "Forced Recirculates -- count only those caused by SMC hits due to an ifetch and load to same cache line or a pending WT store"}, */
	{347, 347, str_nil, "L2_FORCE_RECIRC_SNP_OR_L3"},	/* { "L2_FORCE_RECIRC_SNP_OR_L3", {0x600b4}, 0x10, 4, {0x4220007}, "Forced Recirculates -- count only those caused by a snoop or L3 issue"}, */
	{348, 348, str_nil, "L2_FORCE_RECIRC_TAG_NOTOK"},	/* { "L2_FORCE_RECIRC_TAG_NOTOK", {0x400b4}, 0x10, 4, {0x4220007}, "Forced Recirculates -- count only those caused by L2 hits caused by in flight snoops, stores with a sibling miss to the same index, sibling probe to the same line or pending sync.ia instructions."}, */
	{349, 349, str_nil, "L2_FORCE_RECIRC_TRAN_PREF"},	/* { "L2_FORCE_RECIRC_TRAN_PREF", {0x500b4}, 0x10, 4, {0x4220007}, "Forced Recirculates -- count only those caused by transforms to prefetches"}, */
	{350, 350, str_nil, "L2_FORCE_RECIRC_VIC_BUF_FULL"},	/* { "L2_FORCE_RECIRC_VIC_BUF_FULL", {0xb00b4}, 0x10, 4, {0x4220007}, "Forced Recirculates -- count only those caused by an L2 miss with victim buffer full"}, */
	{351, 351, str_nil, "L2_FORCE_RECIRC_VIC_PEND"},	/* { "L2_FORCE_RECIRC_VIC_PEND", {0x800b4}, 0x10, 4, {0x4220007}, "Forced Recirculates -- count only those caused by an L2 miss with pending victim"}, */
	{352, 352, str_nil, "L2_GOT_RECIRC_IFETCH_ANY"},	/* { "L2_GOT_RECIRC_IFETCH_ANY", {0x800ba}, 0xf0, 1, {0x4420007}, "Instruction Fetch Recirculates Received by L2D -- Instruction fetch recirculates received by L2"}, */
	{353, 353, str_nil, "L2_GOT_RECIRC_OZQ_ACC"},	/* { "L2_GOT_RECIRC_OZQ_ACC", {0xb6}, 0xf0, 1, {0x4220007}, "Counts Number of OZQ Accesses Recirculated to L1D"}, */
	{354, 354, str_nil, "L2_IFET_CANCELS_ANY"},	/* { "L2_IFET_CANCELS_ANY", {0xa1}, 0xf0, 1, {0x4020007}, "Instruction Fetch Cancels by the L2 -- total instruction fetch cancels by L2"}, */
	{355, 355, str_nil, "L2_IFET_CANCELS_BYPASS"},	/* { "L2_IFET_CANCELS_BYPASS", {0x200a1}, 0xf0, 1, {0x4020007}, "Instruction Fetch Cancels by the L2 -- ifetch cancels due to bypassing"}, */
	{356, 356, str_nil, "L2_IFET_CANCELS_CHG_PRIO"},	/* { "L2_IFET_CANCELS_CHG_PRIO", {0xc00a1}, 0xf0, 1, {0x4020007}, "Instruction Fetch Cancels by the L2 -- ifetch cancels due to change priority"}, */
	{357, 357, str_nil, "L2_IFET_CANCELS_DATA_RD"},	/* { "L2_IFET_CANCELS_DATA_RD", {0x700a1}, 0xf0, 1, {0x4020007}, "Instruction Fetch Cancels by the L2 -- ifetch/prefetch cancels due to a data read"}, */
	{358, 358, str_nil, "L2_IFET_CANCELS_DIDNT_RECIR"},	/* { "L2_IFET_CANCELS_DIDNT_RECIR", {0x400a1}, 0xf0, 1, {0x4020007}, "Instruction Fetch Cancels by the L2 -- ifetch cancels because it did not recirculate"}, */
	{359, 359, str_nil, "L2_IFET_CANCELS_IFETCH_BYP"},	/* { "L2_IFET_CANCELS_IFETCH_BYP", {0xd00a1}, 0xf0, 1, {0x4020007}, "Instruction Fetch Cancels by the L2 -- due to ifetch bypass during last clock"}, */
	{360, 360, str_nil, "L2_IFET_CANCELS_PREEMPT"},	/* { "L2_IFET_CANCELS_PREEMPT", {0x800a1}, 0xf0, 1, {0x4020007}, "Instruction Fetch Cancels by the L2 -- ifetch cancels due to preempts"}, */
	{361, 361, str_nil, "L2_IFET_CANCELS_RECIR_OVER_SUB"},	/* { "L2_IFET_CANCELS_RECIR_OVER_SUB", {0x500a1}, 0xf0, 1, {0x4020007}, "Instruction Fetch Cancels by the L2 -- ifetch cancels because of recirculate oversubscription"}, */
	{362, 362, str_nil, "L2_IFET_CANCELS_ST_FILL_WB"},	/* { "L2_IFET_CANCELS_ST_FILL_WB", {0x600a1}, 0xf0, 1, {0x4020007}, "Instruction Fetch Cancels by the L2 -- ifetch cancels due to a store or fill or write back"}, */
	{363, 363, str_nil, "L2_INST_DEMAND_READS"},	/* { "L2_INST_DEMAND_READS", {0x42}, 0xf0, 1, {0xf00001}, "L2 Instruction Demand Fetch Requests"}, */
	{364, 364, str_nil, "L2_INST_PREFETCHES"},	/* { "L2_INST_PREFETCHES", {0x45}, 0xf0, 1, {0xf00001}, "L2 Instruction Prefetch Requests"}, */
	{365, 365, str_nil, "L2_ISSUED_RECIRC_IFETCH_ANY"},	/* { "L2_ISSUED_RECIRC_IFETCH_ANY", {0x800b9}, 0xf0, 1, {0x4420007}, "Instruction Fetch Recirculates Issued by L2 -- Instruction fetch recirculates issued by L2"}, */
	{366, 366, str_nil, "L2_ISSUED_RECIRC_OZQ_ACC"},	/* { "L2_ISSUED_RECIRC_OZQ_ACC", {0xb5}, 0xf0, 1, {0x4220007}, "Count Number of Times a Recirculate Issue Was Attempted and Not Preempted"}, */
	{367, 367, str_nil, "L2_L3ACCESS_CANCEL_ANY"},	/* { "L2_L3ACCESS_CANCEL_ANY", {0x900b0}, 0x10, 1, {0x4120007}, "Canceled L3 Accesses -- count cancels due to any reason. This umask will count more than the sum of all the other umasks. It will count things that weren't committed accesses when they reached L1w, but the L2 attempted to bypass them to the L3 anyway (speculatively). This will include accesses made repeatedly while the main pipeline is stalled and the L1d is attempting to recirculate an access down the L1d pipeline. Thus, an access could get counted many times before it really does get bypassed to the L3. It is a measure of how many times we asserted a request to the L3 but didn't confirm it."}, */
	{368, 368, str_nil, "L2_L3ACCESS_CANCEL_DFETCH"},	/* { "L2_L3ACCESS_CANCEL_DFETCH", {0xa00b0}, 0x10, 1, {0x4120007}, "Canceled L3 Accesses -- data fetches"}, */
	{369, 369, str_nil, "L2_L3ACCESS_CANCEL_EBL_REJECT"},	/* { "L2_L3ACCESS_CANCEL_EBL_REJECT", {0x800b0}, 0x10, 1, {0x4120007}, "Canceled L3 Accesses -- ebl rejects"}, */
	{370, 370, str_nil, "L2_L3ACCESS_CANCEL_FILLD_FULL"},	/* { "L2_L3ACCESS_CANCEL_FILLD_FULL", {0x200b0}, 0x10, 1, {0x4120007}, "Canceled L3 Accesses -- filld being full"}, */
	{371, 371, str_nil, "L2_L3ACCESS_CANCEL_IFETCH"},	/* { "L2_L3ACCESS_CANCEL_IFETCH", {0xb00b0}, 0x10, 1, {0x4120007}, "Canceled L3 Accesses -- instruction fetches"}, */
	{372, 372, str_nil, "L2_L3ACCESS_CANCEL_INV_L3_BYP"},	/* { "L2_L3ACCESS_CANCEL_INV_L3_BYP", {0x600b0}, 0x10, 1, {0x4120007}, "Canceled L3 Accesses -- invalid L3 bypasses"}, */
	{373, 373, str_nil, "L2_L3ACCESS_CANCEL_SPEC_L3_BYP"},	/* { "L2_L3ACCESS_CANCEL_SPEC_L3_BYP", {0x100b0}, 0x10, 1, {0x4120007}, "Canceled L3 Accesses -- speculative L3 bypasses"}, */
	{374, 374, str_nil, "L2_L3ACCESS_CANCEL_UC_BLOCKED"},	/* { "L2_L3ACCESS_CANCEL_UC_BLOCKED", {0x500b0}, 0x10, 1, {0x4120007}, "Canceled L3 Accesses -- Uncacheable blocked L3 Accesses"}, */
	{375, 375, "L2_data_misses", "L2_MISSES"},	/* { "L2_MISSES", {0xcb}, 0xf0, 1, {0xf00007}, "L2 Misses"}, */
	{376, 376, str_nil, "L2_OPS_ISSUED_FP_LOAD"},	/* { "L2_OPS_ISSUED_FP_LOAD", {0x900b8}, 0xf0, 4, {0x4420007}, "Different Operations Issued by L2D -- Count only valid floating point loads"}, */
	{377, 377, str_nil, "L2_OPS_ISSUED_INT_LOAD"},	/* { "L2_OPS_ISSUED_INT_LOAD", {0x800b8}, 0xf0, 4, {0x4420007}, "Different Operations Issued by L2D -- Count only valid integer loads"}, */
	{378, 378, str_nil, "L2_OPS_ISSUED_NST_NLD"},	/* { "L2_OPS_ISSUED_NST_NLD", {0xc00b8}, 0xf0, 4, {0x4420007}, "Different Operations Issued by L2D -- Count only valid non-load, no-store accesses"}, */
	{379, 379, str_nil, "L2_OPS_ISSUED_RMW"},	/* { "L2_OPS_ISSUED_RMW", {0xa00b8}, 0xf0, 4, {0x4420007}, "Different Operations Issued by L2D -- Count only valid read_modify_write stores"}, */
	{380, 380, str_nil, "L2_OPS_ISSUED_STORE"},	/* { "L2_OPS_ISSUED_STORE", {0xb00b8}, 0xf0, 4, {0x4420007}, "Different Operations Issued by L2D -- Count only valid non-read_modify_write stores"}, */
	{381, 381, str_nil, "L2_OZDB_FULL_THIS"},	/* { "L2_OZDB_FULL_THIS", {0xbd}, 0xf0, 1, {0x4520000}, "L2 OZ Data Buffer Is Full -- L2 OZ Data Buffer is full"}, */
	{382, 382, str_nil, "L2_OZQ_ACQUIRE"},	/* { "L2_OZQ_ACQUIRE", {0xa2}, 0xf0, 1, {0x4020000}, "Clocks With Acquire Ordering Attribute Existed in L2 OZQ"}, */
	{383, 383, str_nil, "L2_OZQ_CANCELS0_ANY"},	/* { "L2_OZQ_CANCELS0_ANY", {0xa0}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Late or Any) -- counts the total OZ Queue cancels"}, */
	{384, 384, str_nil, "L2_OZQ_CANCELS0_LATE_ACQUIRE"},	/* { "L2_OZQ_CANCELS0_LATE_ACQUIRE", {0x300a0}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Late or Any) -- counts the late cancels caused by acquires"}, */
	{385, 385, str_nil, "L2_OZQ_CANCELS0_LATE_BYP_EFFRELEASE"},	/* { "L2_OZQ_CANCELS0_LATE_BYP_EFFRELEASE", {0x400a0}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Late or Any) -- counts the late cancels caused by L1D to L2A bypass effective releases"}, */
	{386, 386, str_nil, "L2_OZQ_CANCELS0_LATE_RELEASE"},	/* { "L2_OZQ_CANCELS0_LATE_RELEASE", {0x200a0}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Late or Any) -- counts the late cancels caused by releases"}, */
	{387, 387, str_nil, "L2_OZQ_CANCELS0_LATE_SPEC_BYP"},	/* { "L2_OZQ_CANCELS0_LATE_SPEC_BYP", {0x100a0}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Late or Any) -- counts the late cancels caused by speculative bypasses"}, */
	{388, 388, str_nil, "L2_OZQ_CANCELS1_BANK_CONF"},	/* { "L2_OZQ_CANCELS1_BANK_CONF", {0x100ac}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 1) -- bank conflicts"}, */
	{389, 389, str_nil, "L2_OZQ_CANCELS1_CANC_L2M_ST"},	/* { "L2_OZQ_CANCELS1_CANC_L2M_ST", {0x600ac}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 1) -- caused by a canceled store in L2M"}, */
	{390, 390, str_nil, "L2_OZQ_CANCELS1_CCV"},	/* { "L2_OZQ_CANCELS1_CCV", {0x900ac}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 1) -- a ccv"}, */
	{391, 391, str_nil, "L2_OZQ_CANCELS1_ECC"},	/* { "L2_OZQ_CANCELS1_ECC", {0xf00ac}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 1) -- ECC hardware detecting a problem"}, */
	{392, 392, str_nil, "L2_OZQ_CANCELS1_HPW_IFETCH_CONF"},	/* { "L2_OZQ_CANCELS1_HPW_IFETCH_CONF", {0x500ac}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 1) -- a ifetch conflict (canceling HPW?)"}, */
	{393, 393, str_nil, "L2_OZQ_CANCELS1_L1DF_L2M"},	/* { "L2_OZQ_CANCELS1_L1DF_L2M", {0xe00ac}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 1) -- L1D fill in L2M"}, */
	{394, 394, str_nil, "L2_OZQ_CANCELS1_L1_FILL_CONF"},	/* { "L2_OZQ_CANCELS1_L1_FILL_CONF", {0x700ac}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 1) -- an L1 fill conflict"}, */
	{395, 395, str_nil, "L2_OZQ_CANCELS1_L2A_ST_MAT"},	/* { "L2_OZQ_CANCELS1_L2A_ST_MAT", {0xd00ac}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 1) -- a store match in L2A"}, */
	{396, 396, str_nil, "L2_OZQ_CANCELS1_L2D_ST_MAT"},	/* { "L2_OZQ_CANCELS1_L2D_ST_MAT", {0x200ac}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 1) -- a store match in L2D"}, */
	{397, 397, str_nil, "L2_OZQ_CANCELS1_L2M_ST_MAT"},	/* { "L2_OZQ_CANCELS1_L2M_ST_MAT", {0xb00ac}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 1) -- a store match in L2M"}, */
	{398, 398, str_nil, "L2_OZQ_CANCELS1_MFA"},	/* { "L2_OZQ_CANCELS1_MFA", {0xc00ac}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 1) -- a memory fence instruction"}, */
	{399, 399, str_nil, "L2_OZQ_CANCELS1_REL"},	/* { "L2_OZQ_CANCELS1_REL", {0xac}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 1) -- caused by release"}, */
	{400, 400, str_nil, "L2_OZQ_CANCELS1_SEM"},	/* { "L2_OZQ_CANCELS1_SEM", {0xa00ac}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 1) -- a semaphore"}, */
	{401, 401, str_nil, "L2_OZQ_CANCELS1_ST_FILL_CONF"},	/* { "L2_OZQ_CANCELS1_ST_FILL_CONF", {0x800ac}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 1) -- a store fill conflict"}, */
	{402, 402, str_nil, "L2_OZQ_CANCELS1_SYNC"},	/* { "L2_OZQ_CANCELS1_SYNC", {0x400ac}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 1) -- caused by sync.i"}, */
	{403, 403, str_nil, "L2_OZQ_CANCELS2_ACQ"},	/* { "L2_OZQ_CANCELS2_ACQ", {0x400a8}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 2) -- caused by an acquire"}, */
	{404, 404, str_nil, "L2_OZQ_CANCELS2_CANC_L2C_ST"},	/* { "L2_OZQ_CANCELS2_CANC_L2C_ST", {0x100a8}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 2) -- caused by a canceled store in L2C"}, */
	{405, 405, str_nil, "L2_OZQ_CANCELS2_CANC_L2D_ST"},	/* { "L2_OZQ_CANCELS2_CANC_L2D_ST", {0xd00a8}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 2) -- caused by a canceled store in L2D"}, */
	{406, 406, str_nil, "L2_OZQ_CANCELS2_DIDNT_RECIRC"},	/* { "L2_OZQ_CANCELS2_DIDNT_RECIRC", {0x900a8}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 2) -- caused because it did not recirculate"}, */
	{407, 407, str_nil, "L2_OZQ_CANCELS2_D_IFET"},	/* { "L2_OZQ_CANCELS2_D_IFET", {0xf00a8}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 2) -- a demand ifetch"}, */
	{408, 408, str_nil, "L2_OZQ_CANCELS2_L2C_ST_MAT"},	/* { "L2_OZQ_CANCELS2_L2C_ST_MAT", {0x200a8}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 2) -- a store match in L2C"}, */
	{409, 409, str_nil, "L2_OZQ_CANCELS2_L2FILL_ST_CONF"},	/* { "L2_OZQ_CANCELS2_L2FILL_ST_CONF", {0x800a8}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 2) -- a L2fill and store conflict in L2C"}, */
	{410, 410, str_nil, "L2_OZQ_CANCELS2_OVER_SUB"},	/* { "L2_OZQ_CANCELS2_OVER_SUB", {0xc00a8}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 2) -- oversubscription"}, */
	{411, 411, str_nil, "L2_OZQ_CANCELS2_OZ_DATA_CONF"},	/* { "L2_OZQ_CANCELS2_OZ_DATA_CONF", {0x600a8}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 2) -- an OZ data conflict"}, */
	{412, 412, str_nil, "L2_OZQ_CANCELS2_READ_WB_CONF"},	/* { "L2_OZQ_CANCELS2_READ_WB_CONF", {0x500a8}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 2) -- a write back conflict (canceling read?)"}, */
	{413, 413, str_nil, "L2_OZQ_CANCELS2_RECIRC_OVER_SUB"},	/* { "L2_OZQ_CANCELS2_RECIRC_OVER_SUB", {0xa8}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 2) -- caused by a recirculate oversubscription"}, */
	{414, 414, str_nil, "L2_OZQ_CANCELS2_SCRUB"},	/* { "L2_OZQ_CANCELS2_SCRUB", {0x300a8}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 2) -- 32/64 byte HPW/L2D fill which needs scrub"}, */
	{415, 415, str_nil, "L2_OZQ_CANCELS2_WEIRD"},	/* { "L2_OZQ_CANCELS2_WEIRD", {0xa00a8}, 0xf0, 4, {0x4020007}, "L2 OZQ Cancels (Specific Reason Set 2) -- counts the cancels caused by attempted 5-cycle bypasses for non-aligned accesses and bypasses blocking recirculates for too long"}, */
	{416, 416, str_nil, "L2_OZQ_FULL_THIS"},	/* { "L2_OZQ_FULL_THIS", {0xbc}, 0xf0, 1, {0x4520000}, "L2D OZQ Is Full -- L2D OZQ is full"}, */
	{417, 417, str_nil, "L2_OZQ_RELEASE"},	/* { "L2_OZQ_RELEASE", {0xa3}, 0xf0, 1, {0x4020000}, "Clocks With Release Ordering Attribute Existed in L2 OZQ"}, */
	{418, 418, str_nil, "L2_REFERENCES"},	/* { "L2_REFERENCES", {0xb1}, 0xf0, 4, {0x4120007}, "Requests Made To L2"}, */
	{419, 419, str_nil, "L2_STORE_HIT_SHARED_ANY"},	/* { "L2_STORE_HIT_SHARED_ANY", {0xba}, 0xf0, 2, {0x4320007}, "Store Hit a Shared Line -- Store hit a shared line"}, */
	{420, 420, str_nil, "L2_SYNTH_PROBE"},	/* { "L2_SYNTH_PROBE", {0xb7}, 0xf0, 1, {0x4220007}, "Synthesized Probe"}, */
	{421, 421, str_nil, "L2_VICTIMB_FULL_THIS"},	/* { "L2_VICTIMB_FULL_THIS", {0xbe}, 0xf0, 1, {0x4520000}, "L2D Victim Buffer Is Full -- L2D victim buffer is full"}, */
	{422, 422, str_nil, "L3_LINES_REPLACED"},	/* { "L3_LINES_REPLACED", {0xdf}, 0xf0, 1, {0xf00000}, "L3 Cache Lines Replaced"}, */
	{423, 423, "L3_data_misses", "L3_MISSES"},	/* { "L3_MISSES", {0xdc}, 0xf0, 1, {0xf00007}, "L3 Misses"}, */
	{424, 424, str_nil, "L3_READS_ALL_ALL"},	/* { "L3_READS_ALL_ALL", {0xf00dd}, 0xf0, 1, {0xf00007}, "L3 Reads -- L3 Read References"}, */
	{425, 425, str_nil, "L3_READS_ALL_HIT"},	/* { "L3_READS_ALL_HIT", {0xd00dd}, 0xf0, 1, {0xf00007}, "L3 Reads -- L3 Read Hits"}, */
	{426, 426, str_nil, "L3_READS_ALL_MISS"},	/* { "L3_READS_ALL_MISS", {0xe00dd}, 0xf0, 1, {0xf00007}, "L3 Reads -- L3 Read Misses"}, */
	{427, 427, str_nil, "L3_READS_DATA_READ_ALL"},	/* { "L3_READS_DATA_READ_ALL", {0xb00dd}, 0xf0, 1, {0xf00007}, "L3 Reads -- L3 Load References (excludes reads for ownership used to satisfy stores)"}, */
	{428, 428, str_nil, "L3_READS_DATA_READ_HIT"},	/* { "L3_READS_DATA_READ_HIT", {0x900dd}, 0xf0, 1, {0xf00007}, "L3 Reads -- L3 Load Hits (excludes reads for ownership used to satisfy stores)"}, */
	{429, 429, str_nil, "L3_READS_DATA_READ_MISS"},	/* { "L3_READS_DATA_READ_MISS", {0xa00dd}, 0xf0, 1, {0xf00007}, "L3 Reads -- L3 Load Misses (excludes reads for ownership used to satisfy stores)"}, */
	{430, 430, str_nil, "L3_READS_DINST_FETCH_ALL"},	/* { "L3_READS_DINST_FETCH_ALL", {0x300dd}, 0xf0, 1, {0xf00007}, "L3 Reads -- L3 Demand Instruction References"}, */
	{431, 431, str_nil, "L3_READS_DINST_FETCH_HIT"},	/* { "L3_READS_DINST_FETCH_HIT", {0x100dd}, 0xf0, 1, {0xf00007}, "L3 Reads -- L3 Demand Instruction Fetch Hits"}, */
	{432, 432, str_nil, "L3_READS_DINST_FETCH_MISS"},	/* { "L3_READS_DINST_FETCH_MISS", {0x200dd}, 0xf0, 1, {0xf00007}, "L3 Reads -- L3 Demand Instruction Fetch Misses"}, */
	{433, 433, str_nil, "L3_READS_INST_FETCH_ALL"},	/* { "L3_READS_INST_FETCH_ALL", {0x700dd}, 0xf0, 1, {0xf00007}, "L3 Reads -- L3 Instruction Fetch and Prefetch References"}, */
	{434, 434, str_nil, "L3_READS_INST_FETCH_HIT"},	/* { "L3_READS_INST_FETCH_HIT", {0x500dd}, 0xf0, 1, {0xf00007}, "L3 Reads -- L3 Instruction Fetch and Prefetch Hits"}, */
	{435, 435, str_nil, "L3_READS_INST_FETCH_MISS"},	/* { "L3_READS_INST_FETCH_MISS", {0x600dd}, 0xf0, 1, {0xf00007}, "L3 Reads -- L3 Instruction Fetch and Prefetch Misses"}, */
	{436, 436, str_nil, "L3_REFERENCES"},	/* { "L3_REFERENCES", {0xdb}, 0xf0, 1, {0xf00007}, "L3 References"}, */
	{437, 437, str_nil, "L3_WRITES_ALL_ALL"},	/* { "L3_WRITES_ALL_ALL", {0xf00de}, 0xf0, 1, {0xf00007}, "L3 Writes -- L3 Write References"}, */
	{438, 438, str_nil, "L3_WRITES_ALL_HIT"},	/* { "L3_WRITES_ALL_HIT", {0xd00de}, 0xf0, 1, {0xf00007}, "L3 Writes -- L3 Write Hits"}, */
	{439, 439, str_nil, "L3_WRITES_ALL_MISS"},	/* { "L3_WRITES_ALL_MISS", {0xe00de}, 0xf0, 1, {0xf00007}, "L3 Writes -- L3 Write Misses"}, */
	{440, 440, str_nil, "L3_WRITES_DATA_WRITE_ALL"},	/* { "L3_WRITES_DATA_WRITE_ALL", {0x700de}, 0xf0, 1, {0xf00007}, "L3 Writes -- L3 Store References (excludes L2 write backs, includes L3 read for ownership requests that satisfy stores)"}, */
	{441, 441, str_nil, "L3_WRITES_DATA_WRITE_HIT"},	/* { "L3_WRITES_DATA_WRITE_HIT", {0x500de}, 0xf0, 1, {0xf00007}, "L3 Writes -- L3 Store Hits (excludes L2 write backs, includes L3 read for ownership requests that satisfy stores)"}, */
	{442, 442, str_nil, "L3_WRITES_DATA_WRITE_MISS"},	/* { "L3_WRITES_DATA_WRITE_MISS", {0x600de}, 0xf0, 1, {0xf00007}, "L3 Writes -- L3 Store Misses (excludes L2 write backs, includes L3 read for ownership requests that satisfy stores)"}, */
	{443, 443, str_nil, "L3_WRITES_L2_WB_ALL"},	/* { "L3_WRITES_L2_WB_ALL", {0xb00de}, 0xf0, 1, {0xf00007}, "L3 Writes -- L2 Write Back References"}, */
	{444, 444, str_nil, "L3_WRITES_L2_WB_HIT"},	/* { "L3_WRITES_L2_WB_HIT", {0x900de}, 0xf0, 1, {0xf00007}, "L3 Writes -- L2 Write Back Hits"}, */
	{445, 445, str_nil, "L3_WRITES_L2_WB_MISS"},	/* { "L3_WRITES_L2_WB_MISS", {0xa00de}, 0xf0, 1, {0xf00007}, "L3 Writes -- L2 Write Back Misses"}, */
	{446, 446, str_nil, "LOADS_RETIRED"},	/* { "LOADS_RETIRED", {0xcd}, 0xf0, 4, {0x5310007}, "Retired Loads"}, */
	{447, 447, str_nil, "MEM_READ_CURRENT_ANY"},	/* { "MEM_READ_CURRENT_ANY", {0x30089}, 0xf0, 1, {0xf00000}, "Current Mem Read Transactions On Bus -- CPU or non-CPU (all transactions)."}, */
	{448, 448, str_nil, "MEM_READ_CURRENT_IO"},	/* { "MEM_READ_CURRENT_IO", {0x10089}, 0xf0, 1, {0xf00000}, "Current Mem Read Transactions On Bus -- non-CPU priority agents"}, */
	{449, 449, str_nil, "MISALIGNED_LOADS_RETIRED"},	/* { "MISALIGNED_LOADS_RETIRED", {0xce}, 0xf0, 4, {0x5310007}, "Retired Misaligned Load Instructions"}, */
	{450, 450, str_nil, "MISALIGNED_STORES_RETIRED"},	/* { "MISALIGNED_STORES_RETIRED", {0xd2}, 0xf0, 2, {0x5410007}, "Retired Misaligned Store Instructions"}, */
	{451, 451, str_nil, "NOPS_RETIRED"},	/* { "NOPS_RETIRED", {0x50}, 0xf0, 6, {0xf00003}, "Retired NOP Instructions"}, */
	{452, 452, str_nil, "PREDICATE_SQUASHED_RETIRED"},	/* { "PREDICATE_SQUASHED_RETIRED", {0x51}, 0xf0, 6, {0xf00003}, "Instructions Squashed Due to Predicate Off"}, */
	{453, 453, str_nil, "RSE_CURRENT_REGS_2_TO_0"},	/* { "RSE_CURRENT_REGS_2_TO_0", {0x2b}, 0xf0, 7, {0xf00000}, "Current RSE Registers (Bits 2:0)"}, */
	{454, 454, str_nil, "RSE_CURRENT_REGS_5_TO_3"},	/* { "RSE_CURRENT_REGS_5_TO_3", {0x2a}, 0xf0, 7, {0xf00000}, "Current RSE Registers (Bits 5:3)"}, */
	{455, 455, str_nil, "RSE_CURRENT_REGS_6"},	/* { "RSE_CURRENT_REGS_6", {0x26}, 0xf0, 1, {0xf00000}, "Current RSE Registers (Bit 6)"}, */
	{456, 456, str_nil, "RSE_DIRTY_REGS_2_TO_0"},	/* { "RSE_DIRTY_REGS_2_TO_0", {0x29}, 0xf0, 7, {0xf00000}, "Dirty RSE Registers (Bits 2:0)"}, */
	{457, 457, str_nil, "RSE_DIRTY_REGS_5_TO_3"},	/* { "RSE_DIRTY_REGS_5_TO_3", {0x28}, 0xf0, 7, {0xf00000}, "Dirty RSE Registers (Bits 5:3)"}, */
	{458, 458, str_nil, "RSE_DIRTY_REGS_6"},	/* { "RSE_DIRTY_REGS_6", {0x24}, 0xf0, 1, {0xf00000}, "Dirty RSE Registers (Bit 6)"}, */
	{459, 459, str_nil, "RSE_EVENT_RETIRED"},	/* { "RSE_EVENT_RETIRED", {0x32}, 0xf0, 1, {0xf00000}, "Retired RSE operations"}, */
	{460, 460, str_nil, "RSE_REFERENCES_RETIRED_ALL"},	/* { "RSE_REFERENCES_RETIRED_ALL", {0x30020}, 0xf0, 2, {0xf00007}, "RSE Accesses -- Both RSE loads and stores will be counted."}, */
	{461, 461, str_nil, "RSE_REFERENCES_RETIRED_LOAD"},	/* { "RSE_REFERENCES_RETIRED_LOAD", {0x10020}, 0xf0, 2, {0xf00007}, "RSE Accesses -- Only RSE loads will be counted."}, */
	{462, 462, str_nil, "RSE_REFERENCES_RETIRED_STORE"},	/* { "RSE_REFERENCES_RETIRED_STORE", {0x20020}, 0xf0, 2, {0xf00007}, "RSE Accesses -- Only RSE stores will be counted."}, */
	{463, 463, str_nil, "SERIALIZATION_EVENTS"},	/* { "SERIALIZATION_EVENTS", {0x53}, 0xf0, 1, {0xf00000}, "Number of srlz.i Instructions"}, */
	{464, 464, str_nil, "STORES_RETIRED"},	/* { "STORES_RETIRED", {0xd1}, 0xf0, 2, {0x5410007}, "Retired Stores"}, */
	{465, 465, str_nil, "SYLL_NOT_DISPERSED_ALL"},	/* { "SYLL_NOT_DISPERSED_ALL", {0xf004e}, 0xf0, 5, {0xf00001}, "Syllables Not Dispersed -- Counts all syllables not dispersed. NOTE: Any combination of b0000-b1111 is valid."}, */
	{466, 466, str_nil, "SYLL_NOT_DISPERSED_EXPL"},	/* { "SYLL_NOT_DISPERSED_EXPL", {0x1004e}, 0xf0, 5, {0xf00001}, "Syllables Not Dispersed -- Count syllables not dispersed due to explicit stop bits. These consist of  programmer specified architected S-bit and templates 1 and 5. Dispersal takes a 6-syllable (3-syllable) hit for every template 1/5 in bundle 0(1). Dispersal takes a 3-syllable (0 syllable) hit for every S-bit in bundle 0(1)"}, */
	{467, 467, str_nil, "SYLL_NOT_DISPERSED_FE"},	/* { "SYLL_NOT_DISPERSED_FE", {0x4004e}, 0xf0, 5, {0xf00001}, "Syllables Not Dispersed -- Count syllables not dispersed due to front-end not providing valid bundles or providing valid illegal templates. Dispersal takes a 3-syllable hit for every invalid bundle or valid illegal template from front-end. Bundle 1 with front-end fault, is counted here (3-syllable hit).."}, */
	{468, 468, str_nil, "SYLL_NOT_DISPERSED_IMPL"},	/* { "SYLL_NOT_DISPERSED_IMPL", {0x2004e}, 0xf0, 5, {0xf00001}, "Syllables Not Dispersed -- Count syllables not dispersed due to implicit stop bits. These consist of all of the non-architected stop bits (asymmetry, oversubscription, implicit). Dispersal takes a 6-syllable(3-syllable) hit for every implicit stop bits in bundle 0(1)."}, */
	{469, 469, str_nil, "SYLL_NOT_DISPERSED_MLI"},	/* { "SYLL_NOT_DISPERSED_MLI", {0x8004e}, 0xf0, 5, {0xf00001}, "Syllables Not Dispersed -- Count syllables not dispersed due to MLI bundle and resteers to non-0 syllable. Dispersal takes a 1 syllable hit for each MLI bundle . Dispersal could take 0-2 syllable hit depending on which syllable we resteer to. Bundle 1 with front-end fault which is split, is counted here (0-2 syllable hit)."}, */
	{470, 470, str_nil, "SYLL_OVERCOUNT_ALL"},	/* { "SYLL_OVERCOUNT_ALL", {0x3004f}, 0xf0, 2, {0xf00001}, "Syllables Overcounted -- syllables overcounted in implicit & explicit bucket"}, */
	{471, 471, str_nil, "SYLL_OVERCOUNT_EXPL"},	/* { "SYLL_OVERCOUNT_EXPL", {0x1004f}, 0xf0, 2, {0xf00001}, "Syllables Overcounted -- Only syllables overcounted in the explicit bucket"}, */
	{472, 472, str_nil, "SYLL_OVERCOUNT_IMPL"},	/* { "SYLL_OVERCOUNT_IMPL", {0x2004f}, 0xf0, 2, {0xf00001}, "Syllables Overcounted -- Only syllables overcounted in the implicit bucket"}, */
	{473, 473, str_nil, "UC_LOADS_RETIRED"},	/* { "UC_LOADS_RETIRED", {0xcf}, 0xf0, 4, {0x5310007}, "Retired Uncacheable Loads"}, */
	{474, 474, str_nil, "UC_STORES_RETIRED"},	/* { "UC_STORES_RETIRED", {0xd0}, 0xf0, 2, {0x5410007}, "Retired Uncacheable Stores"}, */
	{179, 179, str_nil, str_nil}
};

#endif /* #if PFMLIB_MAJ_VERSION(PFMLIB_VERSION) > 2 */

#elif ( defined(HW_IRIX64) && defined(HW_IP27) )	/* R10k, R12k */

#define X_NUMEVENTS 32
event_t X_event[X_NUMEVENTS + 1] = {
	{0, -1, "cycles", "Cycles"},	/* "cycles" */
	{1, -1, str_nil, "Decoded instructions"},	/* "instructions" */
	{2, -1, str_nil, "Decoded loads"},	/* "loads" */
	{3, -1, str_nil, "Decoded stores"},	/* "stores" */
	{4, -1, str_nil, "Miss handling table occupancy"},	/* "cond_stores" */
	{5, -1, str_nil, "Failed store conditionals"},	/* "cond_stores_fail" */
	{6, -1, "branches", "Resolved conditional branches"},	/* "branches_resolved" */
	{7, -1, str_nil, "Quadwords written back from secondary cache"},	/* "L2_writebacks" */
	{8, -1, str_nil, "Correctable secondary cache data array ECC errors"},	/* "L2_ECCerrors" */
	{9, -1, "L1_inst_misses", "Primary (L1) instruction cache misses"},	/* "L1_ins_misses" */
	{10, -1, "L2_inst_misses", "Secondary (L2) instruction cache misses"},	/* "L2_ins_misses" */
	{11, -1, str_nil, "Instruction misprediction from L2 cache way prediction table"},	/* "L2_ins_mispredicts" */
	{12, -1, str_nil, "External interventions"},	/* "interventions_reqs" */
	{13, -1, str_nil, "External invalidations"},	/* "invalidations_reqs" */
	{14, -1, str_nil, "ALU/FPU progress cycles (==0)"},	/* "func_completion_cycles" */
	{15, -1, str_nil, "Graduated instructions"},	/* "instructions_grad" */
	{-1, 16, str_nil, "Executed prefetch instructions"},	/* "cycles" */
	{-1, 17, str_nil, "Prefetch primary data cache misses"},	/* "instructions_grad" */
	{-1, 18, str_nil, "Graduated loads"},	/* "loads_grad" */
	{-1, 19, str_nil, "Graduated stores"},	/* "stores_grad" */
	{-1, 20, str_nil, "Graduated store conditionals"},	/* "cond_stores_grad" */
	{-1, 21, str_nil, "Graduated floating-point instructions"},	/* "fp_ops_grad" */
	{-1, 22, str_nil, "Quadwords written back from primary data cache"},	/* "L1_writebacks" */
	{-1, 23, "TLB_misses", "TLB misses"},	/* "TLB_misses" */
	{-1, 24, "branch_misses", "Mispredicted branches"},	/* "branches_mispredict" */
	{-1, 25, "L1_data_misses", "Primary data cache misses"},	/* "L1_data_misses" */
	{-1, 26, "L2_data_misses", "Secondary data cache misses"},	/* "L2_data_misses" */
	{-1, 27, str_nil, "Data misprediction from secondary cache way prediction table"},	/* "L2_data_mispredicts" */
	{-1, 28, str_nil, "State of intervention hits in secondary cache (L2)"},	/* "intervention_hits" */
	{-1, 29, str_nil, "State of invalidation hits in secondary cache"},	/* "invalidation_hits" */
	{-1, 30, str_nil, "Store/prefetch exclusive to clean block in secondary cache"},	/* "L2_clean_excl" */
	{-1, 31, str_nil, "Store/prefetch exclusive to shared block in secondary cache"},	/* "L2_shared_excl" */
	{00, 16, str_nil, str_nil}
};

#elif ( defined(HW_SunOS) && defined(HW_sun4u) )	/* UltraSparc I&II */

#if   defined(HAVE_LIBPERFMON)	/* Solaris <= 7 */
#include <sys/types.h>
#include <sys/processor.h>
#include <sys/procset.h>
#include <perfmon.h>
#elif defined(HAVE_LIBCPC)	/* Solaris >= 8 */
int CPUver = -1;

#include <libcpc.h>
#endif

#define X_NUMEVENTS 24
event_t X_event[X_NUMEVENTS + 1] = {
	{0, 12, "cycles", "CYCLE_CNT", LIB_SunOS(PCR_S0_CYCLE_CNT, "Cycle_cnt")},
	{1, 13, str_nil, "INSTR_CNT", LIB_SunOS(PCR_S0_INSTR_CNT, "Instr_cnt")},
	{2, -1, str_nil, "STALL_IC_MISS", LIB_SunOS(PCR_S0_STALL_IC_MISS, "Dispatch0_IC_miss")},
	{3, -1, str_nil, "STALL_STORBUF", LIB_SunOS(PCR_S0_STALL_STORBUF, "Dispatch0_storeBuf")},
	{4, 16, "L1_inst_misses", "IC_REF", LIB_SunOS(PCR_S0_IC_REF, "IC_ref")},
	{5, 17, "L1_data_misses", "DC_READ", LIB_SunOS(PCR_S0_DC_READ, "DC_rd")},
	{6, 18, str_nil, "DC_WRITE", LIB_SunOS(PCR_S0_DC_WRITE, "DC_wr")},
	{7, -1, str_nil, "STALL_LOAD", LIB_SunOS(PCR_S0_STALL_LOAD, "Load_use")},
	{8, 20, "L2_data_misses", "EC_REF", LIB_SunOS(PCR_S0_EC_REF, "EC_ref")},
	{9, -1, str_nil, "EC_WRITE_RO", LIB_SunOS(PCR_S0_EC_WRITE_RO, "EC_write_hit_RDO")},
	{10, -1, str_nil, "EC_SNOOP_INV", LIB_SunOS(PCR_S0_EC_SNOOP_INV, "EC_snoop_inv")},
	{11, -1, str_nil, "EC_READ_HIT", LIB_SunOS(PCR_S0_EC_READ_HIT, "EC_rd_hit")},
	{0, 12, "cycles", "CYCLE_CNT", LIB_SunOS(PCR_S1_CYCLE_CNT, "Cycle_cnt")},
	{1, 13, str_nil, "INSTR_CNT", LIB_SunOS(PCR_S1_INSTR_CNT, "Instr_cnt")},
	{-1, 14, str_nil, "STALL_MISPRED", LIB_SunOS(PCR_S1_STALL_MISPRED, "Dispatch0_mispred")},
	{-1, 15, str_nil, "STALL_FPDEP", LIB_SunOS(PCR_S1_STALL_FPDEP, "Dispatch0_FP_use")},
	{4, 16, "L1_inst_misses", "IC_HIT", LIB_SunOS(PCR_S1_IC_HIT, "IC_hit")},
	{5, 17, "L1_data_misses", "DC_READ_HIT", LIB_SunOS(PCR_S1_DC_READ_HIT, "DC_rd_hit")},
	{6, 18, str_nil, "DC_WRITE_HIT", LIB_SunOS(PCR_S1_DC_WRITE_HIT, "DC_wr_hit")},
	{-1, 19, str_nil, "LOAD_STALL_RAW", LIB_SunOS(PCR_S1_LOAD_STALL_RAW, "Load_use_RAW")},
	{8, 20, "L2_data_misses", "EC_HIT", LIB_SunOS(PCR_S1_EC_HIT, "EC_hit")},
	{-1, 21, str_nil, "EC_WRITEBACK", LIB_SunOS(PCR_S1_EC_WRITEBACK, "EC_wb")},
	{-1, 22, str_nil, "EC_SNOOP_COPYBCK", LIB_SunOS(PCR_S1_EC_SNOOP_COPYBCK, "EC_snoop_cb")},
	{-1, 23, str_nil, "EC_IC_HIT", LIB_SunOS(PCR_S1_EC_IC_HIT, "EC_ic_hit")},
	{00, 12, str_nil, str_nil, LIB_SunOS(PCR_S0_CYCLE_CNT | PCR_S1_CYCLE_CNT, "Cycle_cnt")}
};

#else /* other OSs or CPUs */

#undef HWCOUNTERS
#define X_NUMEVENTS 0
event_t *X_event = NO_event;

#endif

#else /* no HWCOUNTERS */

#define X_NUMEVENTS 0

#endif


static int
init_counters(void)
{
#if ( defined(HWCOUNTERS) )
#if ( defined(HW_Linux) && defined(HAVE_LIBPPERF) )
	FILE *fp = NULL;
	struct utsname uts;
#elif ( defined(HW_Linux) && defined(HAVE_LIBPERFCTR) )
	struct vperfctr *Self;
#elif ( defined(HW_Linux) && defined(HAVE_LIBPFM) )
	int rtrn = 0;
#endif
#endif

	NumEvents = NoEvent = 0;
#if ( defined(HWCOUNTERS) )
	event = NO_event;
#if ( defined(HW_Linux) && defined(HAVE_LIBPPERF) )
	uname(&uts);
	if (!strcmp(uts.machine, "i586")) {
		NumEvents = NoEvent = P5_NUMEVENTS;
		event = P5_event;
	} else if (!strcmp(uts.machine, "i686")) {
		if ((fp = fopen("/proc/cpuinfo", "r")) == NULL) {
			LOADDEBUG
				mnstr_printf(GDKout,"init_counters: Cannot open /proc/cpuinfo to determine CPU: %s.\n", strerror(errno));
		} else {
			char buf[256];
			char *p1, *p2;
			int found = 0;

			while (fgets(buf, sizeof(buf), fp) != NULL) {
				/* Break the line up at ':' into two parts. */
				p1 = strtok(buf, ":");
				p2 = strtok(NULL, ":");
				/* Get rid of [TAB] in /proc/cpuinfo if it's there */
				p1 = strtok(p1, "\t");
				p2 = strtok(p2, " ");
				p2 = strtok(p2, "\n");
				if (strcmp(p1, "vendor_id") == 0) {
					found++;
					if (!strcmp(p2, "GenuineIntel")) {
						NumEvents = NoEvent = P6_NUMEVENTS;
						event = P6_event;
					} else if (!strcmp(p2, "AuthenticAMD")) {
						NumEvents = NoEvent = K7_NUMEVENTS;
						event = K7_event;
					} else {
						LOADDEBUG
							mnstr_printf(GDKout,"init_counters: Unknown vendor_id '%s` in /proc/cpuinfo.\n", p2);
					}
				}
			}
			if ((!found) && (GDKdebug&LOADMASK))
				mnstr_printf(GDKout,"init_counters: No 'vendor_id` found in /proc/cpuinfo.\n");
			if (found > 1) {
				LOADDEBUG
					mnstr_printf(GDKout,"init_counters: Currently, there's no hardware counter support for Linux SMP machines.\n");
				NumEvents = NoEvent = 0;
				event = NO_event;
			}
			fclose(fp);
		}
	} else {
		LOADDEBUG
			mnstr_printf(GDKout,"init_counters: Architecture '%s' is not supported.\n", uts.machine);
	}
#elif ( defined(HW_Linux) && defined(HAVE_LIBPERFCTR) )
	if (!(Self = vperfctr_open())) {
		LOADDEBUG
			mnstr_printf(GDKout,"init_counters: vperfctr_open failed: %s.\n", strerror(errno));
	} else {
		if (vperfctr_info(Self, &Info) != 0) {
			LOADDEBUG
				mnstr_printf(GDKout,"init_counters: vperfctr_info failed: %s.\n", strerror(errno));
		} else {
			switch (Info.cpu_type) {
			case PERFCTR_X86_INTEL_P5:
			case PERFCTR_X86_INTEL_P5MMX:
			case PERFCTR_X86_CYRIX_MII:
				NumEvents = NoEvent = P5_NUMEVENTS;
				event = P5_event;
				break;
			case PERFCTR_X86_INTEL_P6:
			case PERFCTR_X86_INTEL_PII:
			case PERFCTR_X86_INTEL_PIII:
				NumEvents = NoEvent = P6_NUMEVENTS;
				event = P6_event;
				P6_K7_cesr1.cesr.en = 0;
				break;
			case PERFCTR_X86_AMD_K7:
				NumEvents = NoEvent = K7_NUMEVENTS;
				event = K7_event;
				break;
			case PERFCTR_X86_AMD_K8:
			case PERFCTR_X86_AMD_K8C:
				NumEvents = NoEvent = K8_NUMEVENTS;
				event = K8_event;
				break;
				case PERFCTR_X86_INTEL_P4:
				case PERFCTR_X86_INTEL_P4M2:
				case PERFCTR_X86_INTEL_P4M3:
				NumEvents = NoEvent = P4_NUMEVENTS;
				event = P4_event;
				break;
			}
		}
		if (vperfctr_stop(Self) != 0) {
			LOADDEBUG
				fprintf(stderr, "init_counters: vperfctr_stop failed: %s.\n", strerror(errno));
		}
		if (vperfctr_unlink(Self) != 0) {
			LOADDEBUG
				fprintf(stderr, "init_counters: vperfctr_unlink failed: %s.\n", strerror(errno));
		}
		vperfctr_close(Self);
	}
#elif ( defined(HW_Linux) && defined(HAVE_LIBPFM) )

	/*
	 * Initialize pfm library (required before we can use it)
	 */
	if ((rtrn = pfm_initialize()) != PFMLIB_SUCCESS) {
		LOADDEBUG
			mnstr_printf(GDKout,"init_counters: pfm_initialize failed: %s.\n", pfm_strerror(rtrn));
	} else {
		int pmu_type = 0;

		if ((rtrn = pfm_get_pmu_type(&pmu_type)) != PFMLIB_SUCCESS) {
			LOADDEBUG
				mnstr_printf(GDKout,"init_counters: pfm_get_pmu_type failed: %s.\n", pfm_strerror(rtrn));
		} else {
			pfmlib_options_t pfmlib_options;

			switch (pmu_type) {
			case PFMLIB_ITANIUM_PMU:
				NumEvents = NoEvent = I1_NUMEVENTS;
				event = I1_event;
				break;
			case PFMLIB_ITANIUM2_PMU:
				NumEvents = NoEvent = I2_NUMEVENTS;
				event = I2_event;
				break;
			}
			/*
			 * pass options to library (optional)
			 */
			memset(&pfmlib_options, 0, sizeof(pfmlib_options));
			pfmlib_options.pfm_debug = 0;	/* set to 1 for debug */
			pfm_set_options(&pfmlib_options);
		}
	}

#elif ( defined(HW_SunOS) && defined(HAVE_LIBCPC) )
	if ((cpc_version(CPC_VER_CURRENT) != CPC_VER_CURRENT) || (cpc_version(CPC_VER_CURRENT) == CPC_VER_NONE)) {
		LOADDEBUG
			mnstr_printf(GDKout,"init_counters: library cpc version mismatch!\n");
	} else if ((CPUver = cpc_getcpuver()) == -1) {
		LOADDEBUG
			mnstr_printf(GDKout,"init_counters: no performance counter hardware!");
	} else if (cpc_access() == -1) {
		LOADDEBUG
			mnstr_printf(GDKout,"init_counters: can't access perf counters: %s.", strerror(errno));
	} else {
		NumEvents = NoEvent = X_NUMEVENTS;
		event = X_event;
	}
#else /* no Linux-PC & no Solaris8-Sun */
	NumEvents = NoEvent = X_NUMEVENTS;
	event = X_event;
#endif
#endif
	if ((!NumEvents) && (GDKdebug&LOADMASK))
		mnstr_printf(GDKout,"init_counters: Hardware counters will not be available.\n");
	return GDK_SUCCEED;
}


static int
start_count(counter *retval, int *event0, int *event1)
{
#ifdef HWCOUNTERS
	int e0 = *event0, e1 = *event1, ee = NoEvent;
#else
	(void) event0;
	(void) event1;
#endif

	/* create the resulting counter object */
	memset(retval, 0, sizeof(counter));
	retval->generation = -1;
#if defined(HWCOUNTERS)
	if (e0 < 0 || e0 >= NumEvents)
		e0 = NoEvent;
	if (e1 < 0 || e1 >= NumEvents)
		e1 = NoEvent;
	if (((event[e0].id0 < 0) && (event[e1].id0 >= 0)) || ((event[e1].id1 < 0) && (event[e0].id1 >= 0))) {
		ee = e0;
		e0 = event[e1].id0;
		e1 = event[ee].id1;
	}
	if (event[e0].id0 < 0)
		e0 = NoEvent;
	if (event[e1].id1 < 0)
		e1 = NoEvent;
	if (e0 != NoEvent)
		e0 = event[e0].id0;
	if (e1 != NoEvent)
		e1 = event[e1].id1;
	retval->event0 = (lng) (e0);
	retval->event1 = (lng) (e1);
	if ((e0 != NoEvent) || (e1 != NoEvent)) {
#if ( defined(HW_Linux) && defined(HAVE_LIBPPERF) )
		int rtrn;

		if ((rtrn = start_counters(event[e0].id0, CPL, event[e1].id1, CPL)) != 0) {
			GDKerror("start_count: start_counters failed with return value %d, errno %d.\n", rtrn, errno);
			fprintf(stderr, "! start_count/start_counters: ");
			pstatus(rtrn);
			perror("! start_count/start_counters");
			return GDK_FAIL;
		}
		retval->clocks = rdtsc();
#elif ( defined(HW_Linux) && defined(HAVE_LIBPERFCTR) )
		struct perfctr_sum_ctrs before;
		struct vperfctr_control control;
		struct vperfctr *Self;
		int rtrn;

		if (!(Self = vperfctr_open())) {
			GDKerror("start_count: vperfctr_open failed with error %d.\n", errno);
			perror("! start_count/vperfctr_open");
			return GDK_FAIL;
		}
		retval->generation = (lng) ((ptrdiff_t) Self);
		memset(&control, 0, sizeof(control));
		control.cpu_control.tsc_on = 1;
		control.cpu_control.nractrs = 2;
		control.cpu_control.pmc_map[0] = 0;
		control.cpu_control.pmc_map[1] = 1;
		switch (Info.cpu_type) {
		case PERFCTR_X86_INTEL_P5:
		case PERFCTR_X86_INTEL_P5MMX:
		case PERFCTR_X86_CYRIX_MII:
			P5_cesr.cesr.es0 = event[e0].code;
			P5_cesr.cesr.es1 = event[e1].code;
			control.cpu_control.evntsel[0] = P5_cesr.word;
			break;
		case PERFCTR_X86_INTEL_P6:
		case PERFCTR_X86_INTEL_PII:
		case PERFCTR_X86_INTEL_PIII:
		case PERFCTR_X86_AMD_K7:
		case PERFCTR_X86_AMD_K8:
		case PERFCTR_X86_AMD_K8C:
			P6_K7_cesr0.cesr.evsel = event[e0].code;
			P6_K7_cesr1.cesr.evsel = event[e1].code;
			P6_K7_cesr0.cesr.umask = event[e0].mask;
			P6_K7_cesr1.cesr.umask = event[e1].mask;
			control.cpu_control.evntsel[0] = P6_K7_cesr0.word;
			control.cpu_control.evntsel[1] = P6_K7_cesr1.word;
			break;
			case PERFCTR_X86_INTEL_P4:
			case PERFCTR_X86_INTEL_P4M2:
			case PERFCTR_X86_INTEL_P4M3:
			control.cpu_control.nractrs = 0;
			if (e0 != NoEvent)
				do_event_number( event[e0].code, 0, &control.cpu_control);
			if (e1 != NoEvent)
				do_event_number( event[e1].code, 1, &control.cpu_control);
			break;
		}

		if ((rtrn = vperfctr_control(Self, &control)) != 0) {
			GDKerror("start_count: vperfctr_control failed with return value %d, errno %d.\n", rtrn, errno);
			perror("! start_count/vperfctr_control");
			return GDK_FAIL;
		}
		vperfctr_read_ctrs(Self, &before);
		retval->count0 = (lng) before.pmc[0];
		retval->count1 = (lng) before.pmc[1];
		retval->clocks = (lng) before.tsc;
#elif ( defined(HW_Linux) && defined(HAVE_LIBPFM) )
		int rtrn;
		unsigned int i;
		pfarg_context_t ctx[1];
		pfmInfo_t *pfmInfo = (pfmInfo_t *) malloc(sizeof(pfmInfo_t));

#if PFMLIB_MAJ_VERSION(PFMLIB_VERSION) > 2

		/* PFM for ia64 */
		memset(ctx, 0, sizeof(ctx));
		pfmInfo->fd = 0 ;
		memset(pfmInfo->pc, 0, sizeof(pfmInfo->pc));
		memset(pfmInfo->pd, 0, sizeof(pfmInfo->pd));
		memset(&pfmInfo->inp, 0, sizeof(pfmInfo->inp));
  		memset(&pfmInfo->outp, 0, sizeof(pfmInfo->outp));
		memset(&pfmInfo->load_args, 0, sizeof(pfmInfo->load_args)) ;

		/*
		 * prepare parameters to library. we don't use any Itanium
		 * specific features here. so the pfp_model is NULL.
		 */
		if ((rtrn = pfm_find_event_byname(event[e0].native,
										  &pfmInfo->inp.pfp_events[0].event)) !=
		   	PFMLIB_SUCCESS) {
			GDKerror("start_count: pfm_find_event_byname failed for "
					"event %s: %s\n", event[e0].native, pfm_strerror(rtrn));
			return GDK_FAIL;
		}
		if ((rtrn = pfm_find_event_byname(event[e1].native,
										  &pfmInfo->inp.pfp_events[1].event)) !=
		   	PFMLIB_SUCCESS) {
			GDKerror("start_count: pfm_find_event_byname failed for "
					"event %s: %s\n", event[e1].native, pfm_strerror(rtrn));
			return GDK_FAIL;
		}

		/*
		 * set the default privilege mode for all counters:
		 *      PFM_PLM3 : user level only
		 */
		pfmInfo->inp.pfp_dfl_plm = PFM_PLM3;

		/*
		 * how many counters we use
		 */
		pfmInfo->inp.pfp_event_count = 2;

		/*
		 * let the library figure out the values for the PMCS
		 */
		if ((rtrn = pfm_dispatch_events(&pfmInfo->inp, NULL,
										&pfmInfo->outp, NULL)) !=
			PFMLIB_SUCCESS) {
			GDKerror("start_count: pfm_dispatch_events failed: %s\n",
					pfm_strerror(rtrn));
			return GDK_FAIL;
		}

		/*
		 * copy the library parameters to the OS-specific structures.
		 * Here we propagate the PMC indexes and values.
		 */
		for (i=0; i < pfmInfo->outp.pfp_pmc_count; i++) {
			pfmInfo->pc[i].reg_num   = pfmInfo->outp.pfp_pmcs[i].reg_num;
			pfmInfo->pc[i].reg_value = pfmInfo->outp.pfp_pmcs[i].reg_value;
		}

		/*
		 * propagate the PMC indexes to the PMD arguments to the
		 * kernel. This is required for counting monitors.
		 */
		for (i=0; i < pfmInfo->inp.pfp_event_count; i++) {
			pfmInfo->pd[i].reg_num   = pfmInfo->pc[i].reg_num;
		}

		/*
		 * now create the context for self monitoring/per-task
		 */
		if (perfmonctl(0, PFM_CREATE_CONTEXT, ctx, 1) == -1) {

			if (errno == ENOSYS) {
				GDKerror("start_count: Your kernel does not have "
						"performance monitoring support!\n");
			}
			GDKerror("start_count: Can't create PFM context %s\n",
					strerror(errno));
			return GDK_FAIL;
		}

		/*
		 * extract the file descriptor identifying the context
		 */
		pfmInfo->fd = ctx[0].ctx_fd;

		/*
		 * Now program the PMC registers.
		 * In this case, we write two PMC registers
		 */
		rtrn = perfmonctl(pfmInfo->fd, PFM_WRITE_PMCS, pfmInfo->pc,
						 pfmInfo->outp.pfp_pmc_count);
		if (rtrn == -1) {
			GDKerror( "PFM_WRITE_PMCS errno %d\n",errno);
			exit(1);
		}

		/*
		 * We reset the PMDs that go with the PMCs
		 */
		rtrn = perfmonctl(pfmInfo->fd, PFM_WRITE_PMDS, pfmInfo->pd,
						 pfmInfo->inp.pfp_event_count);
		if (rtrn == -1) {
			GDKerror( "PFM_WRITE_PMDS errno %d\n",errno);
			exit(1);
		}

		/*
		 * attach the perfmon context to ourself
		 */
		pfmInfo->load_args.load_pid = getpid();
		rtrn = perfmonctl(pfmInfo->fd, PFM_LOAD_CONTEXT, &pfmInfo->load_args, 1);
		if (rtrn  == -1) {
			GDKerror( "PFM_LOAD_CONTEXT errno %d\n",errno);
			exit(1);
		}

		retval->generation = (lng) pfmInfo;

		/*
		 * start monitoring. For self-monitoring tasks,
		 * it is possible to
		 * use the lightweight library call instead
		 * of PFM_START
		 */
		pfm_self_start(pfmInfo->fd);

#else /* #if PFMLIB_MAJ_VERSION(PFMLIB_VERSION) > 2 */

		pfmInfo->pid = getpid();
		memset(pfmInfo->pd, 0, sizeof(pfmInfo->pd));
		memset(ctx, 0, sizeof(ctx));

		/*
		 * prepare parameters to library. we don't use any Itanium
		 * specific features here. so the pfp_model is NULL.
		 */
		memset(&pfmInfo->evt, 0, sizeof(pfmInfo->evt));
		if ((rtrn = pfm_find_event_byname(event[e0].native, &pfmInfo->evt.pfp_events[0].event)) != PFMLIB_SUCCESS) {
			GDKerror("start_count: pfm_find_event_byname failed for event %d: %s\n", event[e0].native, pfm_strerror(rtrn));
			return GDK_FAIL;
		}
		if ((rtrn = pfm_find_event_byname(event[e1].native, &pfmInfo->evt.pfp_events[1].event)) != PFMLIB_SUCCESS) {
			GDKerror("start_count: pfm_find_event_byname failed for event %d: %s\n", event[e1].native, pfm_strerror(rtrn));
			return GDK_FAIL;
		}

		/*
		 * set the default privilege mode for all counters:
		 *      PFM_PLM3 : user level only
		 */
		pfmInfo->evt.pfp_dfl_plm = PFM_PLM3;

		/*
		 * how many counters we use
		 */
		pfmInfo->evt.pfp_event_count = 2;

		/*
		 * let the library figure out the values for the PMCS
		 */
		if ((rtrn = pfm_dispatch_events(&pfmInfo->evt)) != PFMLIB_SUCCESS) {
			GDKerror("start_count: pfm_dispatch_events failed: %s\n", pfm_strerror(rtrn));
			return GDK_FAIL;
		}
		/*
		 * for this example, we have decided not to get notified
		 * on counter overflows and the monitoring is not to be inherited
		 * in derived tasks.
		 */
		ctx[0].ctx_flags = PFM_FL_INHERIT_NONE;

		/*
		 * now create the context for self monitoring/per-task
		 */
		if (perfmonctl(pfmInfo->pid, PFM_CREATE_CONTEXT, ctx, 1) == -1) {
			if (errno == ENOSYS) {
				GDKerror("start_count: Your kernel does not have performance monitoring support!\n");
			}
			GDKerror("start_count: Can't create PFM context %s\n", strerror(errno));
			return GDK_FAIL;
		}
		/*
		 * Must be done before any PMD/PMD calls (unfreeze PMU). Initialize
		 * PMC/PMD to safe values. psr.up is cleared.
		 */
		if (perfmonctl(pfmInfo->pid, PFM_ENABLE, NULL, 0) == -1) {
			GDKerror("start_count: perfmonctl error PFM_ENABLE errno %d: %s\n", errno, strerror(errno));
			return GDK_FAIL;
		}

		/*
		 * Now prepare the argument to initialize the PMDs.
		 * the memset(pfmInfo->pd) initialized the entire array to zero already, so
		 * we just have to fill in the register numbers from the pc[] array.
		 */
		for (i = 0; i < pfmInfo->evt.pfp_event_count; i++) {
			pfmInfo->pd[i].reg_num = pfmInfo->evt.pfp_pc[i].reg_num;
		}
		/*
		 * Now program the registers
		 *
		 * We don't use the save variable to indicate the number of elements passed to
		 * the kernel because, as we said earlier, pc may contain more elements than
		 * the number of events we specified, i.e., contains more thann coutning monitors.
		 */
		if (perfmonctl(pfmInfo->pid, PFM_WRITE_PMCS, pfmInfo->evt.pfp_pc, pfmInfo->evt.pfp_pc_count) == -1) {
			GDKerror("start_count: perfmonctl error PFM_WRITE_PMCS errno %d: %s\n", errno, strerror(errno));
			return GDK_FAIL;
		}
		if (perfmonctl(pfmInfo->pid, PFM_WRITE_PMDS, pfmInfo->pd, pfmInfo->evt.pfp_event_count) == -1) {
			{
				unsigned int i;

				for (i = 0; i < pfmInfo->evt.pfp_event_count; i++)
					printf("pmd%d: 0x%x\n", i, pfmInfo->pd[i].reg_flags);
			}
			GDKerror("start_count: perfmonctl error PFM_WRITE_PMDS errno %d: %s\n", errno, strerror(errno));
			return GDK_FAIL;
		}

		retval->generation = (lng) pfmInfo;

		/*
		 * Let's roll now
		 */
		pfm_start();

#endif /* #if PFMLIB_MAJ_VERSION(PFMLIB_VERSION) > 2 */

#elif defined(HW_IRIX64)
		int rtrn;
		int start_counters(int e0, int e1);	/* not in any include file :-( */

		if ((rtrn = start_counters(event[e0].id0, event[e1].id1)) < 0) {
			GDKerror("start_count: start_counters failed with return value %d, errno %d\n", rtrn, errno);
			perror("! start_count/start_counters");
			return GDK_FAIL;
		}
		retval->generation = (lng) rtrn;
#elif ( defined(HW_SunOS) && defined(HAVE_LIBPERFMON) )
		unsigned long long set = 0, val = 0;
		int rtrn;

		if ((rtrn = processor_bind(P_PID, P_MYID, 0, NULL)) < 0) {
			GDKerror("start_count: processor_bind failed with return value %d, errno %d\n", rtrn, errno);
			perror("! start_count/processor_bind");
			return GDK_FAIL;
		}
		if ((rtrn = open("/dev/perfmon", O_RDONLY)) < 0) {
			GDKerror("start_count: open(/dev/perfmon,O_RDONLY) failed with return value %d, errno %d\n", rtrn, errno);
			perror("! start_count/open");
			return GDK_FAIL;
		}
		retval->generation = (lng) rtrn;
		set = (PCR_USER_TRACE | event[e0].bits | event[e1].bits);
		if ((rtrn = ioctl((int) retval->generation, PERFMON_SETPCR, &set)) < 0) {
			GDKerror("start_count: ioctl((int)retval->generation, PERFMON_SETPCR, &set) failed with return value %d, errno %d\n", rtrn, errno);
			perror("! start_count/ioctl");
			close((int) retval->generation);
			return GDK_FAIL;
		}
		clr_pic();
		cpu_sync();
		val = get_pic();
		retval->count0 = (lng) (val & 0xffffffff);
		retval->count1 = (lng) (val >> 32);
		retval->clocks = (lng) get_tick();
#elif ( defined(HW_SunOS) && defined(HAVE_LIBCPC) )
		cpc_event_t evnt;
		char spec[100];
		int rtrn;

		sprintf(spec, "pic0=%s,pic1=%s", event[e0].spec, event[e1].spec);
		if ((rtrn = cpc_strtoevent(CPUver, spec, &evnt)) != 0) {
			GDKerror("start_count: cpc_strtoevent failed with return value %d, errno %d\n", rtrn, errno);
			perror("! start_count/cpc_strtoevent");
			return GDK_FAIL;
		}
		if ((rtrn = cpc_bind_event(&evnt, 0)) != 0) {
			GDKerror("start_count: cpc_bind_event failed with return value %d, errno %d\n", rtrn, errno);
			perror("! start_count/cpc_bind_event");
			return GDK_FAIL;
		}
		if ((rtrn = cpc_take_sample(&evnt)) != 0) {
			GDKerror("start_count: cpc_take_sample failed with return value %d, errno %d\n", rtrn, errno);
			perror("! start_count/cpc_take_sample");
			return GDK_FAIL;
		}
		retval->count0 = (lng) evnt.ce_pic[0];
		retval->count1 = (lng) evnt.ce_pic[1];
		retval->clocks = (lng) evnt.ce_tick;
#endif
	}
#endif
	retval->usec = GDKusec();
	retval->status = 1;
	return GDK_SUCCEED;
}


static int
stop_count(counter *retval, counter *c)
{
	lng usec = GDKusec() - c->usec;
	lng count0 = -1, count1 = -1, clocks = -1;

	if (c->status != 1) {
		GDKerror("stop_count: counter not started or already stopped.\n");
		return GDK_FAIL;
	}
#if defined(HWCOUNTERS)
	if ((c->event0 != NoEvent) || (c->event1 != NoEvent)) {
#if ( defined(HW_Linux) && defined(HAVE_LIBPPERF) )
		lng noclocks = -1;
		dbl notimer = -1.0;
		int rtrn;

		clocks = rdtsc() - c->clocks;
		if ((rtrn = read_counters(&count0, &count1, &notimer, &noclocks)) != 0) {
			GDKerror("stop_count: read_counters failed with return value %d, errno %d.\n", rtrn, errno);
			fprintf(stderr, "! stop_count/read_counters: ");
			pstatus(rtrn);
			perror("! stop_count/read_counters");
			return GDK_FAIL;
		}
#elif ( defined(HW_Linux) && defined(HAVE_LIBPERFCTR) )
		struct perfctr_sum_ctrs after;
		struct vperfctr *Self = (struct vperfctr *) ((ptrdiff_t) c->generation);
		int rtrn;

		vperfctr_read_ctrs(Self, &after);
		count0 = (lng) after.pmc[0] - c->count0;
		count1 = (lng) after.pmc[1] - c->count1;
		clocks = (lng) after.tsc - c->clocks;
		if ((rtrn = vperfctr_stop(Self)) != 0) {
			GDKerror("stop_count: vperfctr_stop failed with return value %d, errno %d.\n", rtrn, errno);
			perror("! stop_count/vperfctr_stop");
			return GDK_FAIL;
		}
		if ((rtrn = vperfctr_unlink(Self)) != 0) {
			GDKerror("stop_count: vperfctr_unlink failed with return value %d, errno %d.\n", rtrn, errno);
			perror("! stop_count/vperfctr_unlink");
			return GDK_FAIL;
		}
		vperfctr_close(Self);
#elif ( defined(HW_Linux) && defined(HAVE_LIBPFM) )

		pfmInfo_t *pfmInfo = (pfmInfo_t *) c->generation;

#if PFMLIB_MAJ_VERSION(PFMLIB_VERSION) > 2

		/*
		 * stop monitoring. For self-monitoring tasks, it is possible to
		 * use the lightweight library call instead of PFM_STOP
		 */
		pfm_self_stop(pfmInfo->fd);

		/*
		 *  now read the results
		 */
		if (perfmonctl(pfmInfo->fd, PFM_READ_PMDS,
						 pfmInfo->pd, pfmInfo->inp.pfp_event_count) == -1) {
			GDKerror( "PFM_READ_PMDS errno %d: %s\n",
					errno, strerror(errno));
			return GDK_FAIL ;
		}

		/* set our return values for our two counters */
		count0 = (lng) pfmInfo->pd[0].reg_value;
		count1 = (lng) pfmInfo->pd[1].reg_value;

		/*
		 * destroy the perfmon context
		 */
		close(pfmInfo->fd);

#else /* #if PFMLIB_MAJ_VERSION(PFMLIB_VERSION) > 2 */

		pfm_stop();

		/*
		 * now read the results
		 */
		if (perfmonctl(pfmInfo->pid, PFM_READ_PMDS, pfmInfo->pd, pfmInfo->evt.pfp_event_count) == -1) {
			GDKerror("stop_count: perfmonctl error READ_PMDS errno %d: %s\n", errno, strerror(errno));
			return GDK_FAIL;
		}
		/*
		 * print the results
		 *
		 * It is important to realize, that the first event we specified may not
		 * be in PMD4. Not all events can be measured by any monitor. That's why
		 * we need to use the pfmInfo->pd[] array to figure out where event i was allocated.
		 *
		 */
		count0 = (lng) pfmInfo->pd[0].reg_value;
		count1 = (lng) pfmInfo->pd[1].reg_value;
		/*
		 * let's stop this now
		 */
		if (perfmonctl(pfmInfo->pid, PFM_DESTROY_CONTEXT, NULL, 0) == -1) {
			GDKerror("stop_count: perfmonctl error PFM_DESTROY errno %d: %s\n", errno, strerror(errno));
			return GDK_FAIL;
		}

#endif /* #if PFMLIB_MAJ_VERSION(PFMLIB_VERSION) > 2 */

#elif defined(HW_IRIX64)
		int rtrn;
		int read_counters(int e0, long long *c0, int e1, long long *c1);

		if ((rtrn = read_counters((int) (event[c->event0].id0), &count0, (int) (event[c->event1].id1), &count1)) < 0) {
			GDKerror("stop_count: read_counters failed with return value %d, errno %d\n", rtrn, errno);
			perror("! stop_count/read_counters");
			return GDK_FAIL;
		}
		if ((lng) rtrn != c->generation) {
			GDKerror("stop_count: lost event counter.\n");
			return GDK_FAIL;
		}
		if (event[c->event0].id0 == 0)
			clocks = count0;
		if (event[c->event1].id1 == 16)
			clocks = count1;
#elif ( defined(HW_SunOS) && defined(HAVE_LIBPERFMON) )
		unsigned long long val = 0;

		clocks = (lng) get_tick() - c->clocks;
		cpu_sync();
		val = get_pic();
		count0 = (lng) (val & 0xffffffff) - c->count0;
		count1 = (lng) (val >> 32) - c->count1;
		close((int) c->generation);
#elif ( defined(HW_SunOS) && defined(HAVE_LIBCPC) )
		cpc_event_t evnt;
		int rtrn;

		if ((rtrn = cpc_take_sample(&evnt)) != 0) {
			GDKerror("stop_count: cpc_take_sample failed with return value %d, errno %d\n", rtrn, errno);
			perror("! stop_count/cpc_take_sample");
			return GDK_FAIL;
		}
		if ((rtrn = cpc_rele()) != 0) {
			GDKerror("stop_count: cpc_rele failed with return value %d, errno %d\n", rtrn, errno);
			perror("! stop_count/cpc_rele");
			return GDK_FAIL;
		}
		count0 = (lng) evnt.ce_pic[0] - c->count0;
		count1 = (lng) evnt.ce_pic[1] - c->count1;
		clocks = (lng) evnt.ce_tick - c->clocks;
#endif
	}
#endif
	/* get the values */
	retval->status = 2;
	retval->generation = c->generation;
	retval->usec = usec;
	retval->clocks = clocks;
	retval->event0 = c->event0;
	if (c->event0 == NoEvent)
		retval->count0 = lng_nil;
	else
		retval->count0 = count0;
	retval->event1 = c->event1;
	if (c->event1 == NoEvent)
		retval->count1 = lng_nil;
	else
		retval->count1 = count1;
	return GDK_SUCCEED;
}


static int
counter2bat(BAT **ret, counter *c)
{
	lng ms;

#if ( defined(HWCOUNTERS) && defined(HW_SunOS) )
	lng diff;
#endif
	if (c->status != 2) {
		GDKerror("counter2bat: counter not stopped.\n");
		return GDK_FAIL;
	}
	ms = c->usec / 1000;
	*ret = BATnew(TYPE_str, TYPE_lng, 8);
	if (*ret == NULL)
		return GDK_FAIL;
	BUNins(*ret, "status", &c->status, FALSE);
	BUNins(*ret, "generation", &c->generation, FALSE);
	BUNins(*ret, "microsecs", &c->usec, FALSE);
	BUNins(*ret, "millisecs", &ms, FALSE);
	BUNins(*ret, "clock_ticks", &c->clocks, FALSE);
#if defined(HWCOUNTERS)
	BUNins(*ret, event[(int) (c->event0)].native, &c->count0, FALSE);
	BUNins(*ret, event[(int) (c->event1)].native, &c->count1, FALSE);
#if defined(HW_SunOS)
	diff = c->count0 - c->count1;
	if ((event[c->event0].id0 == 4) && (event[c->event1].id1 == 16)) {
		BUNins(*ret, "L1_inst_misses = IC_REF - IC_HIT", &diff, FALSE);
	} else if ((event[c->event0].id0 == 5) && (event[c->event1].id1 == 17)) {
		BUNins(*ret, "L1_read_misses = DC_READ - DC_READ_HIT", &diff, FALSE);
	} else if ((event[c->event0].id0 == 6) && (event[c->event1].id1 == 18)) {
		BUNins(*ret, "L1_write_misses = DC_WRITE - DC_WRITE_HIT", &diff, FALSE);
	} else if ((event[c->event0].id0 == 8) && (event[c->event1].id1 == 20)) {
		BUNins(*ret, "L2_data_misses = EC_REF - EC_HIT", &diff, FALSE);
	} else {
		BUNins(*ret, str_nil, &lng_nil, FALSE);
	}
#else
	BUNins(*ret, str_nil, (ptr)&lng_nil, FALSE);
#endif
#endif
/*
	(*ret)->halign = 3928437;
	BATkey(*ret, TRUE);
*/
	BATname(*ret, "counter");
	return GDK_SUCCEED;
}


static int
show_native_events(BAT **ret)
{
#ifdef HWCOUNTERS
	int i = 0;
#endif

	*ret = BATnew(TYPE_int, TYPE_str, NumEvents);
	if (*ret == NULL)
		return GDK_FAIL;
#if defined(HWCOUNTERS)
	for (i = 0; i < NumEvents; i++)
		BUNins(*ret, &i, event[i].native, FALSE);
#endif
	BATkey(*ret, TRUE);
	BATname(*ret, "native_events");
	return GDK_SUCCEED;
}


static int
show_unified_events(BAT **ret)
{
#ifdef HWCOUNTERS
	int i = 0;
#endif

	*ret = BATnew(TYPE_int, TYPE_str, NumEvents);
	if (*ret == NULL)
		return GDK_FAIL;
#if defined(HWCOUNTERS)
	for (i = 0; i < NumEvents; i++)
		if (event[i].unified != str_nil)
			BUNins(*ret, &i, event[i].unified, FALSE);
#endif
	BATkey(*ret, TRUE);
	BATname(*ret, "unified_events");
	return GDK_SUCCEED;
}

/*
 * @- MonetDB Version 5 wrappers
 * The remainder wraps around the M4 library.
 */
#include "mal.h"
#include "mal_exception.h"

static counter cntrs[32];
static int ctop=0;

counters_export str CNTRSinit(int *ret);
str
CNTRSinit(int *ret){
	(void) ret;
	init_counters();
	return MAL_SUCCEED;
}
counters_export str CNTRSstart(int *ret, int *ev1, int *ev2);
str
CNTRSstart(int *ret, int *ev1, int *ev2){
	if( ctop== 32)
		throw(MAL, "counters.start", ILLEGAL_ARGUMENT " Out of counter slots");
	start_count(cntrs + ctop, ev1, ev2);
	*ret = ctop++;
	return MAL_SUCCEED;
}
counters_export str CNTRSreset(int *ret, int *idx, int *ev1, int *ev2);
str
CNTRSreset(int *ret, int *idx, int *ev1, int *ev2){
	if( *idx <0 || *idx>= 32)
		throw(MAL, "counters.start", ILLEGAL_ARGUMENT " Counter handle out of range");
	start_count(cntrs + *idx, ev1, ev2);
	(void) ret;
	return MAL_SUCCEED;
}
counters_export str CNTRSstop(int *ret, int *idx);
str
CNTRSstop(int *ret, int *idx){
	(void) ret;
	if( *idx <0 || *idx>= 32)
		throw(MAL, "counters.start", ILLEGAL_ARGUMENT " Counter handle out of range");
	stop_count(cntrs + *idx, cntrs + *idx);
	return MAL_SUCCEED;
}

counters_export str CNTRScounter2bat(int *bid, int *idx);
str
CNTRScounter2bat(int *bid, int *idx){
	BAT *bn=NULL;
	if( *idx <0 || *idx>= 32)
		throw(MAL, "counters.bat", ILLEGAL_ARGUMENT " Counter handle out of range");
	if (cntrs[*idx].status != 2)
		throw(MAL, "counters.bat", ILLEGAL_ARGUMENT " Counter not yet stopped ");
	counter2bat(&bn, cntrs + *idx);
	if( bn == NULL)
		throw(MAL, "counters.bat", ILLEGAL_ARGUMENT " Could not create object");
	*bid= bn->batCacheid;
	BBPkeepref(*bid);
	return MAL_SUCCEED;
}

counters_export str CNTRScounter2str(str *ret, int *idx);
str
CNTRScounter2str(str *ret, int *idx){
	char buf[1024]="";
#if defined(HWCOUNTERS)
#if defined(HW_SunOS)
	lng diff = 0;
#endif
	counter *c;
#endif
	if( *idx <0 || *idx>= 32)
		throw(MAL, "counters.bat", ILLEGAL_ARGUMENT " Counter handle out of range");

#if defined(HWCOUNTERS)
	c= cntrs+ *idx;
#if defined(HW_SunOS)
	diff = c->count0 - c->count1;
	snprintf(buf,1024,"%10lld us %10lld cy %10lld e%s %10lld e%s %10lld eX",
			c->usec, c->clocks,
			c->count0, event[c->event0].native,
			c->count1,  event[c->event1].native, diff);
#else
	snprintf(buf,1024,"%10lld us %10lld cy", c->usec, c->clocks);
#endif
#endif
	*ret= GDKstrdup(buf);
	throw(MAL, "counters.bat","NYI");
}

counters_export str CNTRSnativeEvents(int *ret);
str
CNTRSnativeEvents(int *ret){
	BAT *bn=0;
	show_native_events(&bn);
	if( bn == NULL)
		throw(MAL, "counters.nativeEvents", MAL_MALLOC_FAIL);
	*ret= bn->batCacheid;
	BBPkeepref(*ret);
	return MAL_SUCCEED;
}

counters_export str CNTRSunifiedEvents(int *ret);
str
CNTRSunifiedEvents(int *ret){
	BAT *bn=0;
	show_unified_events(&bn);
	if( bn == NULL)
		throw(MAL, "counters.unifiedEvents", MAL_MALLOC_FAIL);
	*ret= bn->batCacheid;
	BBPkeepref(*ret);
	return MAL_SUCCEED;
}

counters_export str CNTRSeventNumber(int *ret, str *nme);
str
CNTRSeventNumber(int *ret, str *nme){
#if defined(HWCOUNTERS)
	int i;
	*ret = int_nil;
	for (i = 0; i < NumEvents; i++)
	if(strcmp(*nme, event[i].native)== 0){
		*ret = i;
		return MAL_SUCCEED;
	}
	for (i = 0; i < NumEvents; i++)
	if(strcmp(*nme, event[i].unified)== 0){
		*ret = i;
		return MAL_SUCCEED;
	}
#else
	*ret = int_nil;
#endif
	/* lookup */
	(void) nme;
	return MAL_SUCCEED;
}
counters_export str CNTRSeventName(str *ret, int *nr);
str
CNTRSeventName(str *ret, int *nr){
	(void) nr;
	*ret = (str)str_nil;
#if defined(HWCOUNTERS)
	if( *nr <NumEvents){
		*ret= GDKstrdup(event[*nr].native);
		return MAL_SUCCEED;
	}
#endif
	return MAL_SUCCEED;
}
