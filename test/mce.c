/***
 * Copyright 2023, zhuguangtao@iie.ac.cn, SKLOIS
 * --------------------------------------------------------------
 * Partially borrowed from https://github.com/seL4/sel4test.git
 * Ref: https://github.com/seL4/seL4_libs/tree/master/libsel4test
 * --------------------------------------------------------------
 * 
 * These codes are for CapBuddy's [M]onte [C]arlo Method simulation
 * for [M]emory [R]equests. (abbrev. MR-MC), as we may also 'MR-MC'
 * 'mce' to note Monte Carlo Experiment.
 * 
 */
#include <autoconf.h>
#include <kernel/gen_config.h>
#include <sel4vka/gen_config.h>
#include <sel4allocman/gen_config.h>
#include <sel4testcase-mce/gen_config.h>
#include <sel4muslcsys/gen_config.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <limits.h>
#include <sel4runtime.h>
#include <allocman/bootstrap.h>
#include <allocman/vka.h>
#include <sel4utils/vspace.h>
#include <sel4utils/stack.h>
#include <sel4utils/process.h>
#include <sel4test/test.h>
#include <simple/simple.h>
#include <simple-default/simple-default.h>
#include <utils/util.h>
#include <vka/object.h>
#include <vka/capops.h>
#include <vspace/vspace.h>
#include <sel4platsupport/platsupport.h>
#include <sel4/benchmark_utilisation_types.h>

struct mrmc_env {
    /***
     * Three basic components, which are necessarily needed by user-level initial thread,
     * are provided here: vka (allocator interface object), vspace (page-table manager),
     * simple (default execution environment)
     */
    vka_t vka;
    vspace_t vspace;
    simple_t simple;
};
typedef struct mrmc_env *mrmc_env_t;

#define BRK_VIRTUAL_DEFAULT_SIZE    (1ul << (seL4_PageBits + 10))
#define ALLOCATOR_VIRTUAL_POOL_SIZE (1ul << (seL4_PageBits + 10))
#define ALLOCATOR_STATIC_POOL_SIZE  (1ul << (seL4_PageBits +  5))

/* The global environment variable for libOS */
static struct mrmc_env env;

static sel4utils_alloc_data_t data;

/* Provided with .bss space for libOS environment bootstrapping */
static char allocator_mem_pool[ALLOCATOR_STATIC_POOL_SIZE];

#if (CONFIG_LIB_SEL4_MUSLC_SYS_MORECORE_BYTES == 0)
/* Define these variables so as we can use malloc/calloc/free */
extern vspace_t *muslc_this_vspace;
extern reservation_t muslc_brk_reservation;
extern void *muslc_brk_reservation_start;
#endif

#define MRMC_FUNC_ENTRY __func_entry
#ifndef ENABLE_CAPBUDDY_EXTENSION
#define ENABLE_CAPBUDDY_EXTENSION \
    (config_set(CONFIG_LIB_VKA_ALLOW_POOL_OPERATIONS) && \
     config_set(CONFIG_LIB_ALLOCMAN_ALLOW_POOL_OPERATIONS))
#endif

static void init_env(mrmc_env_t env)
{
    /***
     * We should now initialize the allocator (with libsel4allocman utilities) first.
     * One bootstrap allocator with single-level cspace would be enough for 'MR-MC'.
     * 
     * At the very first, only static pool is provided because during bootstrap it's
     * forbidden for the thread to use virtual space to meet dynamic memory requests.
     */
    allocman_t *allocman = bootstrap_use_current_simple(&env->simple,
                                                        ALLOCATOR_STATIC_POOL_SIZE, allocator_mem_pool);
    if (allocman == NULL) {
        ZF_LOGF("Failed to create allocman");
    }
    /* Bind vka (allocator interface) with allocman allocator instance. */
    allocman_make_vka(&env->vka, allocman);

    /***
     * Initialize the vspace manager and bind it to the thread's executing environment
     * This is necessary because we need to initialize two allocator utilities for dynamic
     * memory requests, and before doing that, we must reserve the memory regions (in
     * virtual address space) for them in vspace manager.
     */
    int err = sel4utils_bootstrap_vspace_with_bootinfo_leaky(&env->vspace,
                                                             &data, simple_get_pd(&env->simple),
                                                             &env->vka, platsupport_get_bootinfo());
    if (err) {
        ZF_LOGF("Failed to bootstrap vspace");
    }
#if (CONFIG_LIB_SEL4_MUSLC_SYS_MORECORE_BYTES == 0)
    sel4utils_res_t *muslc_brk_reservation_memory = allocman_mspace_alloc(allocman, sizeof(sel4utils_res_t), &err);
    if (err) {
        ZF_LOGE("Failed to alloc virtual memory for muslc heap describing metadata");
    }
    /***
     * We now start to reserve the memory region for muslibcsys utilities, these memory regions
     * act like the user heap for the thread. By means of initializing them, codes with POSIX's
     * interfaces are available. (malloc/calloc/free)
     */
    err = sel4utils_reserve_range_no_alloc(&env->vspace, muslc_brk_reservation_memory,
                                           BRK_VIRTUAL_DEFAULT_SIZE, seL4_AllRights, 1, &muslc_brk_reservation_start);
    if (err) {
        ZF_LOGE("Failed to reserve range for muslc heap initialization");
    }
    /* Binds the global muslibcsys variables */
    muslc_this_vspace = &env->vspace;
    muslc_brk_reservation.res = muslc_brk_reservation_memory;
#endif
    void *vaddr;
    reservation_t virtual_reservation;
    /***
     * We need to configure on virtual pool for vka->[allocman] allocator, this is because
     * the allocator itself will use different interfaces (other than POSIX's malloc/calloc/free)
     * to apply memory requests for allocator metadata to help managing kernel objects' user-level
     * information.
     */
    virtual_reservation = vspace_reserve_range(&env->vspace,
                                               ALLOCATOR_VIRTUAL_POOL_SIZE, seL4_AllRights, 1, &vaddr);
    /***
     * We need to make sure that the regions for memory requests, which reside in virtual address space,
     * are successfully reserved by vspace manager, so as new pages can be mapped to activate these
     * memory regions. (dynamic memory requests server for allocator's metadata)
     */
    if (virtual_reservation.res == 0) {
        ZF_LOGE("Failed to provide virtual memory for allocator");
    }
    /* Now configure the virtual pool (by initializing new interfaces that will require memory dynamically) */
    bootstrap_configure_virtual_pool(allocman, vaddr,
                                     ALLOCATOR_VIRTUAL_POOL_SIZE, simple_get_pd(&env->simple));
}

/***
 * Random Sequences (based on different policies) of memory request
 * size (alloc/free size per iteration timestamp)
 */
int get_random_size_policy_1()
{
    int a = random() % 1024;
    while (a == 0)
    {
        a = random() % 1024;
    }
    return a;
}

int get_random_size_policy_2()
{
    int a[11] = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024};
    int b = random() % 11;
    return a[b];
}

int get_random_size_policy_3()
{
    int a[11] = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024};
    int b = (random() % 1024) + 1;
    if (b == 1)
    {
        return a[10];
    }
    else if (b == 2)
    {
        return a[9];
    }
    else if (2 < b && b <= 4)
    {
        return a[8];
    }
    else if (4 < b && b <= 8)
    {
        return a[7];
    }
    else if (8 < b && b <= 16)
    {
        return a[6];
    }
    else if (16 < b && b <= 32)
    {
        return a[5];
    }
    else if (32 < b && b <= 64)
    {
        return a[4];
    }
    else if (64 < b && b <= 128)
    {
        return a[3];
    }
    else if (128 < b && b <= 256)
    {
        return a[2];
    }
    else if (256 < b && b <= 512)
    {
        return a[1];
    }
    else if (512 < b && b <= 1024)
    {
        return a[0];
    }
}

/* Random Sequence of memory request timestamp */
int get_random_time(int a)
{
    int b = random() % a;
    while (b == 0)
    {
        b = random() % a;
    }
    return b;
}

#define MCMC_ITERATION_TIME 80000
#define MCMC_FREE_FREQUENCY 80

/**
 * Monte Carlo experiment simulation function:
 *  1. CapBuddy enable (on split-based allocator).
 *  2. cascading split-based buddy allocator only.
 */
#if (ENABLE_CAPBUDDY_EXTENSION)

typedef struct mcmc_memory_unit {
    /**
     * We use time_stamp to denote when this piece of memory unit
     * is allocated in case that we can free it later on.
     */
    size_t time_stamp;

    size_t iter_stamp;
    /**
     * We don't save the number of frames that we need in here
     *   so as to save memory space ...
     * By means of computing:
     *   frame_number = (compressed_frames->size_bits / seL4_PageBits);
     */
    vka_object_t *compressed_frames;
    /**
     * All mcmc_memory_unit will have to construct a list.
     */
    struct mcmc_memory_unit *next;

} mcmc_memory_unit_t;

static mcmc_memory_unit_t *mcmc_memory_unit_list_head;
static mcmc_memory_unit_t *mcmc_memory_unit_list_curr;

static int mcmc_exp_simulation()
{
    vka_t *vka = &env.vka;
    seL4_Word frame_type = kobject_get_type(KOBJECT_FRAME, 12);
    size_t memory_footprint = 0;
    int mcmc_errno = seL4_NoError;

    int i = 1;
    size_t total = 0;
    /**
     * ta: to-allocate;
     * tf: to-free;
     * time_stamp: a point within the MCMC time serie.
     * frame_count: number of frames to alloc/free at this time.
     */
    uint32_t ta_frame_sanitizer, ta_frame_count;
    int ta_time_stamp, ta_block_size;

    mcmc_memory_unit_list_head = (mcmc_memory_unit_t *)malloc(sizeof(mcmc_memory_unit_t));
    mcmc_memory_unit_list_head->time_stamp = 0;
    mcmc_memory_unit_list_head->iter_stamp = 0;
    mcmc_memory_unit_list_head->next = NULL;
    mcmc_memory_unit_list_curr = mcmc_memory_unit_list_head;

    mcmc_memory_unit_t *tx;
    mcmc_memory_unit_t *tp;

    for (; i < MCMC_ITERATION_TIME; ++i)
    {
        ta_time_stamp = get_random_time(MCMC_FREE_FREQUENCY) + i;
        ta_frame_count = get_random_size_policy_2();
        ta_block_size = CTZL(ta_frame_count) + seL4_PageBits;
        /**
         * original untyped object (size: ta_block_size)
         */
        tx = (mcmc_memory_unit_t *)malloc(sizeof(mcmc_memory_unit_t));
        memset(tx, 0, sizeof(*tx));
        /**
         * new memory unit metadata initialization:
        */
        tx->iter_stamp = i;
        tx->time_stamp = ta_time_stamp;
        tx->compressed_frames = (vka_object_t *)malloc(sizeof(vka_object_t));
        memset(tx->compressed_frames, 0, sizeof(*tx->compressed_frames));
        /**
         * We should now allocate frames in here.
         * by means of calling 'vka_alloc_frame_contiguous' can we benefit from
         * allocating contiguous frames in bulk.
        */
        mcmc_errno = vka_alloc_frame_contiguous(vka, ta_block_size, &ta_frame_sanitizer, tx->compressed_frames);
        if (mcmc_errno) {
            printf("[DONE]: total %d iteration %d\n", total - ta_frame_count, i - 1);
            assert(0);
        }

        assert(ta_frame_sanitizer == ta_frame_count);

        mcmc_memory_unit_list_curr->next = tx;
        mcmc_memory_unit_list_curr = tx;

        tx = mcmc_memory_unit_list_head;
        tp = tx->next;

        while (tp)
        {
            if (tp->iter_stamp > i) {
                break;
            }
            if (tp->time_stamp == i)
            {
                /**
                 * We now try to cleanup the original untyped's metadata:
                 *  1. capability slot bookkeeping overhead
                 *  2. kernel object itself (by means of calling vka_free_object)
                 *  3. vka_object_t (at userlevel to describe it)
                */
                vka_free_object(vka, tp->compressed_frames);
                free(tp->compressed_frames);
                /**
                 * tp = tx->next
                 * so we should free current tp, move tp forward and let tx->next equals to new tp
                */
                tp = tp->next;
                free(tx->next);
                tx->next = tp;
            } else {
                tx = tp;
                tp = tp->next;                
            }
        }
        total += ta_frame_count;
    }
    printf("[DONE]: total %d iteration %d\n", total, i);
    return 0;
}

#else

typedef struct mcmc_frame_unit {
    seL4_CPtr frame_cptr;
    struct mcmc_frame_unit *next;
} mcmc_frame_unit_t;

typedef struct mcmc_memory_unit {
    /**
     * We use time_stamp to denote when this piece of memory unit
     * is allocated in case that we can free it later on.
     */
    size_t time_stamp;

    size_t iter_stamp;
    /**
     * We don't save the number of frames that we need in here
     *   so as to save memory space ...
     * By means of computing:
     *   frame_number = (origin_untyped_object->size_bits / seL4_PageBits);
     */
    vka_object_t *origin_untyped_object;
    /**
     * frame_cptr_list (length): size of original untyped object divide seL4_PageBits
     *   Let's say: origin 32k -> 15, frame_cptr_list length: pow(2, 15-12) = 2^3 = 8
     */
    mcmc_frame_unit_t *frame_cptr_list;
    /**
     * All mcmc_memory_unit will have to construct a list.
     */
    struct mcmc_memory_unit *next;

} mcmc_memory_unit_t;

static mcmc_memory_unit_t *mcmc_memory_unit_list_head;
static mcmc_memory_unit_t *mcmc_memory_unit_list_curr;

static int mcmc_exp_simulation()
{
    vka_t *vka = &env.vka;
    seL4_Word frame_type = kobject_get_type(KOBJECT_FRAME, 12);
    size_t memory_footprint = 0;
    int mcmc_errno = seL4_NoError;

    int i = 1;
    size_t total = 0;
    /**
     * ta: to-allocate;
     * tf: to-free;
     * time_stamp: a point within the MCMC time serie.
     * frame_count: number of frames to alloc/free at this time.
     */
    int ta_time_stamp, ta_frame_count, ta_block_size;

    mcmc_memory_unit_list_head = (mcmc_memory_unit_t *)malloc(sizeof(mcmc_memory_unit_t));
    mcmc_memory_unit_list_head->time_stamp = 0;
    mcmc_memory_unit_list_head->iter_stamp = 0;
    mcmc_memory_unit_list_head->next = NULL;
    mcmc_memory_unit_list_curr = mcmc_memory_unit_list_head;

    mcmc_memory_unit_t *tx;
    mcmc_memory_unit_t *tp;
    cspacepath_t tc;

    for (; i <= MCMC_ITERATION_TIME; ++i)
    {
        ta_time_stamp = get_random_time(MCMC_FREE_FREQUENCY) + i;
        ta_frame_count = get_random_size_policy_2();
        ta_block_size = CTZL(ta_frame_count) + seL4_PageBits;
        /**
         * original untyped object (size: ta_block_size)
         */
        tx = (mcmc_memory_unit_t *)malloc(sizeof(mcmc_memory_unit_t));
        memset(tx, 0, sizeof(*tx));

        tx->iter_stamp = i;
        tx->time_stamp = ta_time_stamp;
        tx->origin_untyped_object = (vka_object_t *)malloc(sizeof(vka_object_t));
        memset(tx->origin_untyped_object, 0, sizeof(*tx->origin_untyped_object));
        /**
         * We need to allocate one untyped object (with ta_block_size) first.
         * This is because cascading split-based buddy allocator will allocate
         * frames distributingly if the frames are not from one untyped object.
        */
        mcmc_errno = vka_alloc_object(vka, seL4_UntypedObject, ta_block_size, tx->origin_untyped_object);
        if (mcmc_errno) {
            printf("[DONE]: total %d iteration %d\n", total - ta_frame_count, i - 1);
            assert(0); // abort as error handling (temporarily)
        }
        /**
         * We now need to allocate all frames from the original untyped.
        */
        mcmc_frame_unit_t *ft = NULL;
        mcmc_frame_unit_t *fl = NULL;

        for (int j = 0; j < ta_frame_count; ++j)
        {
            ft = (mcmc_frame_unit_t *)malloc(sizeof(mcmc_frame_unit_t));
            memset(ft, 0, sizeof(*ft));

            mcmc_errno = vka_cspace_alloc(vka, &ft->frame_cptr);
            if (mcmc_errno) {
                assert(0);
            }
            vka_cspace_make_path(vka, ft->frame_cptr, &tc);
            /**
             * create the j(th) frame through untyped retyped.
             * (It's okay to clean / delete these frames through revoke).
             */
            mcmc_errno = vka_untyped_retype(tx->origin_untyped_object, frame_type, seL4_PageBits, 1, &tc);
            if (mcmc_errno) {
                assert(0);
            }
            if (fl) {
                ft->next = fl;
            }
            fl = ft;
        }
        tx->frame_cptr_list = ft;
        /**
         * Add 'tx' into memory_unit_list
        */
        mcmc_memory_unit_list_curr->next = tx;
        mcmc_memory_unit_list_curr = tx;
        /**
         *  Free operations start here:
        */
        tx = mcmc_memory_unit_list_head;
        tp = tx->next;
        
        while (tp)
        {
            if (tp->iter_stamp > i) {
                break;
            }
            if (tp->time_stamp == i)
            {
                /**
                 * We should now revoke these frame capabilities within seL4
                 * through calling seL4_CNode_Revoke on original untyped obj's cap
                */
                vka_cspace_make_path(vka, tp->origin_untyped_object->cptr, &tc);
                mcmc_errno = vka_cnode_revoke(&tc);
                if (mcmc_errno) {
                    assert(0);
                }
                ft = tp->frame_cptr_list;
                fl = ft;
                /**
                 * We should free capability bookkeeping message from cspace
                 * manager (cspace-allocator) for every frames.
                */
                while (ft)
                {
                    vka_cspace_free(vka, ft->frame_cptr);
                    /**
                     * We should free user-level bookkeeping metadata for the
                     * frames in here. (after we freeing it within cspace-allocator)
                    */
                    ft = ft->next;
                    free(fl);
                    fl = ft;
                }
                /**
                 * We now try to cleanup the original untyped's metadata:
                 *  1. capability slot bookkeeping overhead
                 *  2. kernel object itself (by means of calling vka_free_object)
                 *  3. vka_object_t (at userlevel to describe it)
                */
                vka_free_object(vka, tp->origin_untyped_object);
                free(tp->origin_untyped_object);
                /**
                 * tp = tx->next
                 * so we should free current tp, move tp forward and let tx->next equals to new tp
                */
                tp = tp->next;
                free(tx->next);
                tx->next = tp;
            } else {
                tx = tp;
                tp = tp->next;                
            }
        }
        total += ta_frame_count;
    }
    printf("[DONE]: total %d iteration %d\n", total, i);
    return 0;
}

#endif

void *__func_entry(void *arg UNUSED)
{
    int err;
    printf("\n>>>>>>>> __func_entry__ <<<<<<<\n");
#ifdef CONFIG_KERNEL_BENCHMARK
    printf("\n*********** Benchmark ***********\n\n");
    uint64_t *ipcbuffer = (uint64_t *)&(seL4_GetIPCBuffer()->msg[0]);
    seL4_BenchmarkResetThreadUtilisation(simple_get_tcb(&env.simple));
    seL4_BenchmarkResetLog();
#endif
    err = mcmc_exp_simulation();
    if (err != seL4_NoError) {
        assert(0);
    }
#ifdef CONFIG_KERNEL_BENCHMARK
    seL4_BenchmarkFinalizeLog();
    seL4_BenchmarkGetThreadUtilisation(simple_get_tcb(&env.simple));
    printf("\n*********** Benchmark ***********\n");
    printf("\nCPU cycles spent (TCB-Schedule): %llu\n", ipcbuffer[BENCHMARK_TCB_UTILISATION]);
    printf("\nCPU cycles spent (TCB-Kernel): %llu\n", ipcbuffer[BENCHMARK_TCB_KERNEL_UTILISATION]);
#endif
    printf("\n>>>>>>>> __func__exit__ <<<<<<<\n");
}

static void sel4test_exit(int code) {
    /***
     * Suspend first initial thread (which is the thread we are in)
     * (add it into scheduling queue in kernel) through TCB_Suspend
     * interface.
     */
    seL4_TCB_Suspend(seL4_CapInitThreadTCB);
}

int main(void)
{
    void *res;
    sel4runtime_set_exit(sel4test_exit);
    /***
     * Receive bootinfo from sel4runtime, which is provided throughly
     * by seL4 kernel, and use it to create & initial executing environment.
     */
    seL4_BootInfo *info = platsupport_get_bootinfo();
    simple_default_init_bootinfo(&env.simple, info);
    /***
     * (Optional) To print the bootstrapping information.
     */
    if (config_set(CONFIG_MRMC_RESOURCE_INFO)) {
        simple_print(&env.simple);
    }
    /***
     * Initialize environment. Including allocator (allocman) bootstrapping,
     * vspace, I/O operations, etc,.
     */
    init_env(&env);
    /***
     * Start to run it now.
     */
    int error = sel4utils_run_on_stack(&env.vspace, MRMC_FUNC_ENTRY, NULL, &res);
    test_assert_fatal(error == 0);
    return 0;
}
