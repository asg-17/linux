/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __KVM_H
#define __KVM_H

#include <linux/types.h>
#include <linux/hardirq.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/sched/stat.h>
#include <linux/bug.h>
#include <linux/minmax.h>
#include <linux/mm.h>
#include <linux/mmu_notifier.h>
#include <linux/preempt.h>
#include <linux/msi.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/rcupdate.h>
#include <linux/ratelimit.h>
#include <linux/err.h>
#include <linux/irqflags.h>
#include <linux/context_tracking.h>
#include <linux/irqbypass.h>
#include <linux/rcuwait.h>
#include <linux/refcount.h>
#include <linux/nospec.h>
#include <linux/notifier.h>
#include <linux/ftrace.h>
#include <linux/hashtable.h>
#include <linux/instrumentation.h>
#include <linux/interval_tree.h>
#include <linux/rbtree.h>
#include <linux/xarray.h>
#include <asm/signal.h>

#include <linux/kvm.h>
#include <linux/kvm_para.h>

#include "../../virt/kvm/kvm_types.h"

#include <asm/kvm_host.h>

/* Two fragments for cross MMIO pages. */
#define KVM_MAX_MMIO_FRAGMENTS	2

/**
 * kvm_dirty_ring: KVM internal dirty ring structure
 *
 * @dirty_index: free running counter that points to the next slot in
 *               dirty_ring->dirty_gfns, where a new dirty page should go
 * @reset_index: free running counter that points to the next dirty page
 *               in dirty_ring->dirty_gfns for which dirty trap needs to
 *               be reenabled
 * @size:        size of the compact list, dirty_ring->dirty_gfns
 * @soft_limit:  when the number of dirty pages in the list reaches this
 *               limit, vcpu that owns this ring should exit to userspace
 *               to allow userspace to harvest all the dirty pages
 * @dirty_gfns:  the array to keep the dirty gfns
 * @index:       index of this dirty ring
 */
struct kvm_dirty_ring {
	u32 dirty_index;
	u32 reset_index;
	u32 size;
	u32 soft_limit;
	struct kvm_dirty_gfn *dirty_gfns;
	int index;
};

/*
 * Sometimes a large or cross-page mmio needs to be broken up into separate
 * exits for userspace servicing.
 */
struct kvm_mmio_fragment {
	gpa_t gpa;
	void *data;
	unsigned len;
};

struct kvm_vcpu {
	struct kvm *kvm;
#ifdef CONFIG_PREEMPT_NOTIFIERS
	struct preempt_notifier preempt_notifier;
#endif
	int cpu;
	int vcpu_id; /* id given by userspace at creation */
	int vcpu_idx; /* index into kvm->vcpu_array */
	int ____srcu_idx; /* Don't use this directly.  You've been warned. */
#ifdef CONFIG_PROVE_RCU
	int srcu_depth;
#endif
	int mode;
	u64 requests;
	unsigned long guest_debug;

	struct mutex mutex;
	struct kvm_run *run;

#ifndef __KVM_HAVE_ARCH_WQP
	struct rcuwait wait;
#endif
	struct pid __rcu *pid;
	int sigset_active;
	sigset_t sigset;
	unsigned int halt_poll_ns;
	bool valid_wakeup;

#ifdef CONFIG_HAS_IOMEM
	int mmio_needed;
	int mmio_read_completed;
	int mmio_is_write;
	int mmio_cur_fragment;
	int mmio_nr_fragments;
	struct kvm_mmio_fragment mmio_fragments[KVM_MAX_MMIO_FRAGMENTS];
#endif

#ifdef CONFIG_KVM_ASYNC_PF
	struct {
		u32 queued;
		struct list_head queue;
		struct list_head done;
		spinlock_t lock;
	} async_pf;
#endif

#ifdef CONFIG_HAVE_KVM_CPU_RELAX_INTERCEPT
	/*
	 * Cpu relax intercept or pause loop exit optimization
	 * in_spin_loop: set when a vcpu does a pause loop exit
	 *  or cpu relax intercepted.
	 * dy_eligible: indicates whether vcpu is eligible for directed yield.
	 */
	struct {
		bool in_spin_loop;
		bool dy_eligible;
	} spin_loop;
#endif
	bool preempted;
	bool ready;
	struct kvm_vcpu_arch arch;
	struct kvm_vcpu_stat stat;
	char stats_id[KVM_STATS_NAME_SIZE];
	struct kvm_dirty_ring dirty_ring;

	/*
	 * The most recently used memslot by this vCPU and the slots generation
	 * for which it is valid.
	 * No wraparound protection is needed since generations won't overflow in
	 * thousands of years, even assuming 1M memslot operations per second.
	 */
	struct kvm_memory_slot *last_used_slot;
	u64 last_used_slot_gen;
};

#ifdef CONFIG_KVM_XFER_TO_GUEST_WORK
static inline void kvm_handle_signal_exit(struct kvm_vcpu *vcpu)
{
	vcpu->run->exit_reason = KVM_EXIT_INTR;
	vcpu->stat.signal_exits++;
}
#endif /* CONFIG_KVM_XFER_TO_GUEST_WORK */

void kvm_get_kvm(struct kvm *kvm);
bool kvm_get_kvm_safe(struct kvm *kvm);
void kvm_put_kvm(struct kvm *kvm);
bool file_is_kvm(struct file *file);
void kvm_put_kvm_no_destroy(struct kvm *kvm);

#endif
