/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NET_SCHED_GENERIC_H
#define __NET_SCHED_GENERIC_H

#include <linux/netdevice.h>
#include <linux/types.h>
#include <linux/rcupdate.h>
#include <linux/pkt_sched.h>
#include <linux/pkt_cls.h>
#include <linux/percpu.h>
#include <linux/dynamic_queue_limits.h>
#include <linux/list.h>
#include <linux/refcount.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/atomic.h>
#include <linux/hashtable.h>
#include <net/gen_stats.h>
#include <net/rtnetlink.h>
#include <net/flow_offload.h>
#include <linux/xarray.h>

struct Qdisc_ops;
struct qdisc_walker;
struct tcf_walker;
struct module;
struct bpf_flow_keys;

struct qdisc_rate_table {
	struct tc_ratespec rate;
	u32		data[256];
	struct qdisc_rate_table *next;
	int		refcnt;
};

enum qdisc_state_t {
	__QDISC_STATE_SCHED,
	__QDISC_STATE_DEACTIVATED,
	__QDISC_STATE_MISSED,
	__QDISC_STATE_DRAINING,
};

enum qdisc_state2_t {
	/* Only for !TCQ_F_NOLOCK qdisc. Never access it directly.
	 * Use qdisc_run_begin/end() or qdisc_is_running() instead.
	 */
	__QDISC_STATE2_RUNNING,
};

#define QDISC_STATE_MISSED	BIT(__QDISC_STATE_MISSED)
#define QDISC_STATE_DRAINING	BIT(__QDISC_STATE_DRAINING)

#define QDISC_STATE_NON_EMPTY	(QDISC_STATE_MISSED | \
					QDISC_STATE_DRAINING)

struct qdisc_size_table {
	struct rcu_head		rcu;
	struct list_head	list;
	struct tc_sizespec	szopts;
	int			refcnt;
	u16			data[];
};

/* similar to sk_buff_head, but skb->prev pointer is undefined. */
struct qdisc_skb_head {
	struct sk_buff	*head;
	struct sk_buff	*tail;
	__u32		qlen;
	spinlock_t	lock;
};

struct Qdisc {
	/* May return NET_XMIT_BUSY and NOT free skb. */
	int (*try_enqueue)(struct sk_buff *skb, struct Qdisc *dev,
			   struct sk_buff **to_free);

	int 			(*enqueue)(struct sk_buff *skb,
					   struct Qdisc *sch,
					   struct sk_buff **to_free);
	struct sk_buff *	(*dequeue)(struct Qdisc *sch);
	unsigned int		flags;
#define TCQ_F_BUILTIN		1
#define TCQ_F_INGRESS		2
#define TCQ_F_CAN_BYPASS	4
#define TCQ_F_MQROOT		8
#define TCQ_F_ONETXQUEUE	0x10 /* dequeue_skb() can assume all skbs are for
				      * q->dev_queue : It can test
				      * netif_xmit_frozen_or_stopped() before
				      * dequeueing next packet.
				      * Its true for MQ/MQPRIO slaves, or non
				      * multiqueue device.
				      */
#define TCQ_F_WARN_NONWC	(1 << 16)
#define TCQ_F_CPUSTATS		0x20 /* run using percpu statistics */
#define TCQ_F_NOPARENT		0x40 /* root of its hierarchy :
				      * qdisc_tree_decrease_qlen() should stop.
				      */
#define TCQ_F_INVISIBLE		0x80 /* invisible by default in dump */
#define TCQ_F_NOLOCK		0x100 /* qdisc does not require locking */
#define TCQ_F_OFFLOADED		0x200 /* qdisc is offloaded to HW */
	u32			limit;
	const struct Qdisc_ops	*ops;
	struct qdisc_size_table	__rcu *stab;
	struct hlist_node       hash;
	u32			handle;
	u32			parent;

	struct netdev_queue	*dev_queue;

	struct net_rate_estimator __rcu *rate_est;
	struct gnet_stats_basic_sync __percpu *cpu_bstats;
	struct gnet_stats_queue	__percpu *cpu_qstats;
	int			pad;
	refcount_t		refcnt;

	/*
	 * For performance sake on SMP, we put highly modified fields at the end
	 */
	struct sk_buff_head	gso_skb ____cacheline_aligned_in_smp;
	struct qdisc_skb_head	q;
	struct gnet_stats_basic_sync bstats;
	struct gnet_stats_queue	qstats;
	int                     owner;
	unsigned long		state;
	unsigned long		state2; /* must be written under qdisc spinlock */
	struct Qdisc            *next_sched;
	struct sk_buff_head	skb_bad_txq;

	spinlock_t		busylock ____cacheline_aligned_in_smp;
	spinlock_t		seqlock;

	struct rcu_head		rcu;
	netdevice_tracker	dev_tracker;
	struct lock_class_key	root_lock_key;
	/* private data */
	long privdata[] ____cacheline_aligned;
};

static inline void qdisc_refcount_inc(struct Qdisc *qdisc)
{
	if (qdisc->flags & TCQ_F_BUILTIN)
		return;
	refcount_inc(&qdisc->refcnt);
}

static inline bool qdisc_refcount_dec_if_one(struct Qdisc *qdisc)
{
	if (qdisc->flags & TCQ_F_BUILTIN)
		return true;
	return refcount_dec_if_one(&qdisc->refcnt);
}

/* Intended to be used by unlocked users, when concurrent qdisc release is
 * possible.
 */

static inline struct Qdisc *qdisc_refcount_inc_nz(struct Qdisc *qdisc)
{
	if (qdisc->flags & TCQ_F_BUILTIN)
		return qdisc;
	if (refcount_inc_not_zero(&qdisc->refcnt))
		return qdisc;
	return NULL;
}

/* For !TCQ_F_NOLOCK qdisc: callers must either call this within a qdisc
 * root_lock section, or provide their own memory barriers -- ordering
 * against qdisc_run_begin/end() atomic bit operations.
 */
static inline bool qdisc_is_running(struct Qdisc *qdisc)
{
	if (qdisc->flags & TCQ_F_NOLOCK)
		return spin_is_locked(&qdisc->seqlock);
	return test_bit(__QDISC_STATE2_RUNNING, &qdisc->state2);
}

static inline bool nolock_qdisc_is_empty(const struct Qdisc *qdisc)
{
	return !(READ_ONCE(qdisc->state) & QDISC_STATE_NON_EMPTY);
}

static inline bool qdisc_is_percpu_stats(const struct Qdisc *q)
{
	return q->flags & TCQ_F_CPUSTATS;
}

static inline bool qdisc_is_empty(const struct Qdisc *qdisc)
{
	if (qdisc_is_percpu_stats(qdisc))
		return nolock_qdisc_is_empty(qdisc);
	return !READ_ONCE(qdisc->q.qlen);
}

/* For !TCQ_F_NOLOCK qdisc, qdisc_run_begin/end() must be invoked with
 * the qdisc root lock acquired.
 */
static inline bool qdisc_run_begin(struct Qdisc *qdisc)
{
	if (qdisc->flags & TCQ_F_NOLOCK) {
		if (spin_trylock(&qdisc->seqlock))
			return true;

		/* No need to insist if the MISSED flag was already set.
		 * Note that test_and_set_bit() also gives us memory ordering
		 * guarantees wrt potential earlier enqueue() and below
		 * spin_trylock(), both of which are necessary to prevent races
		 */
		if (test_and_set_bit(__QDISC_STATE_MISSED, &qdisc->state))
			return false;

		/* Try to take the lock again to make sure that we will either
		 * grab it or the CPU that still has it will see MISSED set
		 * when testing it in qdisc_run_end()
		 */
		return spin_trylock(&qdisc->seqlock);
	}
	return !__test_and_set_bit(__QDISC_STATE2_RUNNING, &qdisc->state2);
}

static inline void qdisc_run_end(struct Qdisc *qdisc)
{
	if (qdisc->flags & TCQ_F_NOLOCK) {
		spin_unlock(&qdisc->seqlock);

		/* spin_unlock() only has store-release semantic. The unlock
		 * and test_bit() ordering is a store-load ordering, so a full
		 * memory barrier is needed here.
		 */
		smp_mb();

		if (unlikely(test_bit(__QDISC_STATE_MISSED,
				      &qdisc->state)))
			__netif_schedule(qdisc);
	} else {
		__clear_bit(__QDISC_STATE2_RUNNING, &qdisc->state2);
	}
}

static inline bool qdisc_may_bulk(const struct Qdisc *qdisc)
{
	return qdisc->flags & TCQ_F_ONETXQUEUE;
}

static inline int qdisc_avail_bulklimit(const struct netdev_queue *txq)
{
	return netdev_queue_dql_avail(txq);
}

struct Qdisc_class_ops {
	unsigned int		flags;
	/* Child qdisc manipulation */
	struct netdev_queue *	(*select_queue)(struct Qdisc *, struct tcmsg *);
	int			(*graft)(struct Qdisc *, unsigned long cl,
					struct Qdisc *, struct Qdisc **,
					struct netlink_ext_ack *extack);
	struct Qdisc *		(*leaf)(struct Qdisc *, unsigned long cl);
	void			(*qlen_notify)(struct Qdisc *, unsigned long);

	/* Class manipulation routines */
	unsigned long		(*find)(struct Qdisc *, u32 classid);
	int			(*change)(struct Qdisc *, u32, u32,
					struct nlattr **, unsigned long *,
					struct netlink_ext_ack *);
	int			(*delete)(struct Qdisc *, unsigned long,
					  struct netlink_ext_ack *);
	void			(*walk)(struct Qdisc *, struct qdisc_walker * arg);

	/* Filter manipulation */
	struct tcf_block *	(*tcf_block)(struct Qdisc *sch,
					     unsigned long arg,
					     struct netlink_ext_ack *extack);
	unsigned long		(*bind_tcf)(struct Qdisc *, unsigned long,
					u32 classid);
	void			(*unbind_tcf)(struct Qdisc *, unsigned long);

	/* rtnetlink specific */
	int			(*dump)(struct Qdisc *, unsigned long,
					struct sk_buff *skb, struct tcmsg*);
	int			(*dump_stats)(struct Qdisc *, unsigned long,
					struct gnet_dump *);
};

/* Qdisc_class_ops flag values */

/* Implements API that doesn't require rtnl lock */
enum qdisc_class_ops_flags {
	QDISC_CLASS_OPS_DOIT_UNLOCKED = 1,
};

struct Qdisc_ops {
	struct Qdisc_ops	*next;
	const struct Qdisc_class_ops	*cl_ops;
	char			id[IFNAMSIZ];
	int			priv_size;
	unsigned int		static_flags;
	/* May return NET_XMIT_BUSY and NOT free skb. */
	int (*try_enqueue)(struct sk_buff *skb, struct Qdisc *qd, struct sk_buff **to_free);
	int 			(*enqueue)(struct sk_buff *skb,
					   struct Qdisc *sch,
					   struct sk_buff **to_free);

	struct sk_buff *	(*dequeue)(struct Qdisc *);
	struct sk_buff *	(*peek)(struct Qdisc *);

	int			(*init)(struct Qdisc *sch, struct nlattr *arg,
					struct netlink_ext_ack *extack);
	void			(*reset)(struct Qdisc *);
	void			(*destroy)(struct Qdisc *);
	int			(*change)(struct Qdisc *sch,
					  struct nlattr *arg,
					  struct netlink_ext_ack *extack);
	void			(*attach)(struct Qdisc *sch);
	int			(*change_tx_queue_len)(struct Qdisc *, unsigned int);
	void			(*change_real_num_tx)(struct Qdisc *sch,
						      unsigned int new_real_tx);

	int			(*dump)(struct Qdisc *, struct sk_buff *);
	int			(*dump_stats)(struct Qdisc *, struct gnet_dump *);

	void			(*ingress_block_set)(struct Qdisc *sch,
						     u32 block_index);
	void			(*egress_block_set)(struct Qdisc *sch,
						    u32 block_index);
	u32			(*ingress_block_get)(struct Qdisc *sch);
	u32			(*egress_block_get)(struct Qdisc *sch);

	struct module		*owner;
};

struct tcf_result {
	union {
		struct {
			unsigned long	class;
			u32		classid;
		};
		const struct tcf_proto *goto_tp;
	};
};

struct tcf_chain;

struct tcf_proto_ops {
	struct list_head	head;
	char			kind[IFNAMSIZ];

	int			(*classify)(struct sk_buff *,
					    const struct tcf_proto *,
					    struct tcf_result *);
	int			(*init)(struct tcf_proto*);
	void			(*destroy)(struct tcf_proto *tp, bool rtnl_held,
					   struct netlink_ext_ack *extack);

	void*			(*get)(struct tcf_proto*, u32 handle);
	void			(*put)(struct tcf_proto *tp, void *f);
	int			(*change)(struct net *net, struct sk_buff *,
					struct tcf_proto*, unsigned long,
					u32 handle, struct nlattr **,
					void **, u32,
					struct netlink_ext_ack *);
	int			(*delete)(struct tcf_proto *tp, void *arg,
					  bool *last, bool rtnl_held,
					  struct netlink_ext_ack *);
	bool			(*delete_empty)(struct tcf_proto *tp);
	void			(*walk)(struct tcf_proto *tp,
					struct tcf_walker *arg, bool rtnl_held);
	int			(*reoffload)(struct tcf_proto *tp, bool add,
					     flow_setup_cb_t *cb, void *cb_priv,
					     struct netlink_ext_ack *extack);
	void			(*hw_add)(struct tcf_proto *tp,
					  void *type_data);
	void			(*hw_del)(struct tcf_proto *tp,
					  void *type_data);
	void			(*bind_class)(void *, u32, unsigned long,
					      void *, unsigned long);
	void *			(*tmplt_create)(struct net *net,
						struct tcf_chain *chain,
						struct nlattr **tca,
						struct netlink_ext_ack *extack);
	void			(*tmplt_destroy)(void *tmplt_priv);
	void			(*tmplt_reoffload)(struct tcf_chain *chain,
						   bool add,
						   flow_setup_cb_t *cb,
						   void *cb_priv);
	struct tcf_exts *	(*get_exts)(const struct tcf_proto *tp,
					    u32 handle);

	/* rtnetlink specific */
	int			(*dump)(struct net*, struct tcf_proto*, void *,
					struct sk_buff *skb, struct tcmsg*,
					bool);
	int			(*terse_dump)(struct net *net,
					      struct tcf_proto *tp, void *fh,
					      struct sk_buff *skb,
					      struct tcmsg *t, bool rtnl_held);
	int			(*tmplt_dump)(struct sk_buff *skb,
					      struct net *net,
					      void *tmplt_priv);

	struct module		*owner;
	int			flags;
};

/* Classifiers setting TCF_PROTO_OPS_DOIT_UNLOCKED in tcf_proto_ops->flags
 * are expected to implement tcf_proto_ops->delete_empty(), otherwise race
 * conditions can occur when filters are inserted/deleted simultaneously.
 */
enum tcf_proto_ops_flags {
	TCF_PROTO_OPS_DOIT_UNLOCKED = 1,
};

struct tcf_proto {
	/* Fast access part */
	struct tcf_proto __rcu	*next;
	void __rcu		*root;

	/* called under RCU BH lock*/
	int			(*classify)(struct sk_buff *,
					    const struct tcf_proto *,
					    struct tcf_result *);
	__be16			protocol;

	/* All the rest */
	u32			prio;
	void			*data;
	const struct tcf_proto_ops	*ops;
	struct tcf_chain	*chain;
	/* Lock protects tcf_proto shared state and can be used by unlocked
	 * classifiers to protect their private data.
	 */
	spinlock_t		lock;
	bool			deleting;
	bool			counted;
	bool			usesw;
	refcount_t		refcnt;
	struct rcu_head		rcu;
	struct hlist_node	destroy_ht_node;
};

struct qdisc_skb_cb {
	struct {
		unsigned int		pkt_len;
		u16			slave_dev_queue_mapping;
		u16			tc_classid;
	};
#define QDISC_CB_PRIV_LEN 20
	unsigned char		data[QDISC_CB_PRIV_LEN];
};

typedef void tcf_chain_head_change_t(struct tcf_proto *tp_head, void *priv);

struct tcf_chain {
	/* Protects filter_chain. */
	struct mutex filter_chain_lock;
	struct tcf_proto __rcu *filter_chain;
	struct list_head list;
	struct tcf_block *block;
	u32 index; /* chain index */
	unsigned int refcnt;
	unsigned int action_refcnt;
	bool explicitly_created;
	bool flushing;
	const struct tcf_proto_ops *tmplt_ops;
	void *tmplt_priv;
	struct rcu_head rcu;
};

struct tcf_block {
	struct xarray ports; /* datapath accessible */
	/* Lock protects tcf_block and lifetime-management data of chains
	 * attached to the block (refcnt, action_refcnt, explicitly_created).
	 */
	struct mutex lock;
	struct list_head chain_list;
	u32 index; /* block index for shared blocks */
	u32 classid; /* which class this block belongs to */
	refcount_t refcnt;
	struct net *net;
	struct Qdisc *q;
	struct rw_semaphore cb_lock; /* protects cb_list and offload counters */
	struct flow_block flow_block;
	struct list_head owner_list;
	bool keep_dst;
	atomic_t useswcnt;
	atomic_t offloadcnt; /* Number of oddloaded filters */
	unsigned int nooffloaddevcnt; /* Number of devs unable to do offload */
	unsigned int lockeddevcnt; /* Number of devs that require rtnl lock. */
	struct {
		struct tcf_chain *chain;
		struct list_head filter_chain_list;
	} chain0;
	struct rcu_head rcu;
	DECLARE_HASHTABLE(proto_destroy_ht, 7);
	struct mutex proto_destroy_lock; /* Lock for proto_destroy hashtable. */
};

struct tcf_block *tcf_block_lookup(struct net *net, u32 block_index);

static inline bool lockdep_tcf_chain_is_locked(struct tcf_chain *chain)
{
	return lockdep_is_held(&chain->filter_chain_lock);
}

static inline bool lockdep_tcf_proto_is_locked(struct tcf_proto *tp)
{
	return lockdep_is_held(&tp->lock);
}

#define tcf_chain_dereference(p, chain)					\
	rcu_dereference_protected(p, lockdep_tcf_chain_is_locked(chain))

#define tcf_proto_dereference(p, tp)					\
	rcu_dereference_protected(p, lockdep_tcf_proto_is_locked(tp))

static inline void qdisc_cb_private_validate(const struct sk_buff *skb, int sz)
{
	struct qdisc_skb_cb *qcb;

	BUILD_BUG_ON(sizeof(skb->cb) < sizeof(*qcb));
	BUILD_BUG_ON(sizeof(qcb->data) < sz);
}

static inline int qdisc_qlen(const struct Qdisc *q)
{
	return q->q.qlen;
}

static inline int qdisc_qlen_sum(const struct Qdisc *q)
{
	__u32 qlen = q->qstats.qlen;
	int i;

	if (qdisc_is_percpu_stats(q)) {
		for_each_possible_cpu(i)
			qlen += per_cpu_ptr(q->cpu_qstats, i)->qlen;
	} else {
		qlen += q->q.qlen;
	}

	return qlen;
}

static inline struct qdisc_skb_cb *qdisc_skb_cb(const struct sk_buff *skb)
{
	return (struct qdisc_skb_cb *)skb->cb;
}

static inline spinlock_t *qdisc_lock(struct Qdisc *qdisc)
{
	return &qdisc->q.lock;
}

static inline struct Qdisc *qdisc_root(const struct Qdisc *qdisc)
{
	struct Qdisc *q = rcu_dereference_rtnl(qdisc->dev_queue->qdisc);

	return q;
}

static inline struct Qdisc *qdisc_root_bh(const struct Qdisc *qdisc)
{
	return rcu_dereference_bh(qdisc->dev_queue->qdisc);
}

static inline struct Qdisc *qdisc_root_sleeping(const struct Qdisc *qdisc)
{
	return rcu_dereference_rtnl(qdisc->dev_queue->qdisc_sleeping);
}

static inline spinlock_t *qdisc_root_sleeping_lock(const struct Qdisc *qdisc)
{
	struct Qdisc *root = qdisc_root_sleeping(qdisc);

	ASSERT_RTNL();
	return qdisc_lock(root);
}

static inline struct net_device *qdisc_dev(const struct Qdisc *qdisc)
{
	return qdisc->dev_queue->dev;
}

static inline void sch_tree_lock(struct Qdisc *q)
{
	if (q->flags & TCQ_F_MQROOT)
		spin_lock_bh(qdisc_lock(q));
	else
		spin_lock_bh(qdisc_root_sleeping_lock(q));
}

static inline void sch_tree_unlock(struct Qdisc *q)
{
	if (q->flags & TCQ_F_MQROOT)
		spin_unlock_bh(qdisc_lock(q));
	else
		spin_unlock_bh(qdisc_root_sleeping_lock(q));
}

extern struct Qdisc noop_qdisc;
extern struct Qdisc_ops noop_qdisc_ops;
extern struct Qdisc_ops pfifo_fast_ops;
extern const u8 sch_default_prio2band[TC_PRIO_MAX + 1];
extern struct Qdisc_ops mq_qdisc_ops;
extern struct Qdisc_ops noqueue_qdisc_ops;
extern const struct Qdisc_ops *default_qdisc_ops;
static inline const struct Qdisc_ops *
get_default_qdisc_ops(const struct net_device *dev, int ntx)
{
	return ntx < dev->real_num_tx_queues ?
			default_qdisc_ops : &pfifo_fast_ops;
}

struct Qdisc_class_common {
	u32			classid;
	unsigned int		filter_cnt;
	struct hlist_node	hnode;
};

struct Qdisc_class_hash {
	struct hlist_head	*hash;
	unsigned int		hashsize;
	unsigned int		hashmask;
	unsigned int		hashelems;
};

static inline unsigned int qdisc_class_hash(u32 id, u32 mask)
{
	id ^= id >> 8;
	id ^= id >> 4;
	return id & mask;
}

static inline struct Qdisc_class_common *
qdisc_class_find(const struct Qdisc_class_hash *hash, u32 id)
{
	struct Qdisc_class_common *cl;
	unsigned int h;

	if (!id)
		return NULL;

	h = qdisc_class_hash(id, hash->hashmask);
	hlist_for_each_entry(cl, &hash->hash[h], hnode) {
		if (cl->classid == id)
			return cl;
	}
	return NULL;
}

static inline bool qdisc_class_in_use(const struct Qdisc_class_common *cl)
{
	return cl->filter_cnt > 0;
}

static inline void qdisc_class_get(struct Qdisc_class_common *cl)
{
	unsigned int res;

	if (check_add_overflow(cl->filter_cnt, 1, &res))
		WARN(1, "Qdisc class overflow");

	cl->filter_cnt = res;
}

static inline void qdisc_class_put(struct Qdisc_class_common *cl)
{
	unsigned int res;

	if (check_sub_overflow(cl->filter_cnt, 1, &res))
		WARN(1, "Qdisc class underflow");

	cl->filter_cnt = res;
}

static inline int tc_classid_to_hwtc(struct net_device *dev, u32 classid)
{
	u32 hwtc = TC_H_MIN(classid) - TC_H_MIN_PRIORITY;

	return (hwtc < netdev_get_num_tc(dev)) ? hwtc : -EINVAL;
}

int qdisc_class_hash_init(struct Qdisc_class_hash *);
void qdisc_class_hash_insert(struct Qdisc_class_hash *,
			     struct Qdisc_class_common *);
void qdisc_class_hash_remove(struct Qdisc_class_hash *,
			     struct Qdisc_class_common *);
void qdisc_class_hash_grow(struct Qdisc *, struct Qdisc_class_hash *);
void qdisc_class_hash_destroy(struct Qdisc_class_hash *);

int dev_qdisc_change_tx_queue_len(struct net_device *dev);
void dev_qdisc_change_real_num_tx(struct net_device *dev,
				  unsigned int new_real_tx);
void dev_init_scheduler(struct net_device *dev);
void dev_shutdown(struct net_device *dev);
void dev_activate(struct net_device *dev);
void dev_deactivate(struct net_device *dev);
void dev_deactivate_many(struct list_head *head);
struct Qdisc *dev_graft_qdisc(struct netdev_queue *dev_queue,
			      struct Qdisc *qdisc);
void qdisc_reset(struct Qdisc *qdisc);
void qdisc_destroy(struct Qdisc *qdisc);
void qdisc_put(struct Qdisc *qdisc);
void qdisc_put_unlocked(struct Qdisc *qdisc);
void qdisc_tree_reduce_backlog(struct Qdisc *qdisc, int n, int len);
#ifdef CONFIG_NET_SCHED
int qdisc_offload_dump_helper(struct Qdisc *q, enum tc_setup_type type,
			      void *type_data);
void qdisc_offload_graft_helper(struct net_device *dev, struct Qdisc *sch,
				struct Qdisc *new, struct Qdisc *old,
				enum tc_setup_type type, void *type_data,
				struct netlink_ext_ack *extack);
#else
static inline int
qdisc_offload_dump_helper(struct Qdisc *q, enum tc_setup_type type,
			  void *type_data)
{
	q->flags &= ~TCQ_F_OFFLOADED;
	return 0;
}

static inline void
qdisc_offload_graft_helper(struct net_device *dev, struct Qdisc *sch,
			   struct Qdisc *new, struct Qdisc *old,
			   enum tc_setup_type type, void *type_data,
			   struct netlink_ext_ack *extack)
{
}
#endif
void qdisc_offload_query_caps(struct net_device *dev,
			      enum tc_setup_type type,
			      void *caps, size_t caps_len);
struct Qdisc *qdisc_alloc(struct netdev_queue *dev_queue,
			  const struct Qdisc_ops *ops,
			  struct netlink_ext_ack *extack);
void qdisc_free(struct Qdisc *qdisc);
struct Qdisc *qdisc_create_dflt(struct netdev_queue *dev_queue,
				const struct Qdisc_ops *ops, u32 parentid,
				struct netlink_ext_ack *extack);
void __qdisc_calculate_pkt_len(struct sk_buff *skb,
			       const struct qdisc_size_table *stab);
int skb_do_redirect(struct sk_buff *);

static inline bool skb_at_tc_ingress(const struct sk_buff *skb)
{
#ifdef CONFIG_NET_XGRESS
	return skb->tc_at_ingress;
#else
	return false;
#endif
}

static inline bool skb_skip_tc_classify(struct sk_buff *skb)
{
#ifdef CONFIG_NET_CLS_ACT
	if (skb->tc_skip_classify) {
		skb->tc_skip_classify = 0;
		return true;
	}
#endif
	return false;
}

/* Reset all TX qdiscs greater than index of a device.  */
static inline void qdisc_reset_all_tx_gt(struct net_device *dev, unsigned int i)
{
	struct Qdisc *qdisc;

	for (; i < dev->num_tx_queues; i++) {
		qdisc = rtnl_dereference(netdev_get_tx_queue(dev, i)->qdisc);
		if (qdisc) {
			spin_lock_bh(qdisc_lock(qdisc));
			qdisc_reset(qdisc);
			spin_unlock_bh(qdisc_lock(qdisc));
		}
	}
}

/* Are all TX queues of the device empty?  */
static inline bool qdisc_all_tx_empty(const struct net_device *dev)
{
	unsigned int i;

	rcu_read_lock();
	for (i = 0; i < dev->num_tx_queues; i++) {
		struct netdev_queue *txq = netdev_get_tx_queue(dev, i);
		const struct Qdisc *q = rcu_dereference(txq->qdisc);

		if (!qdisc_is_empty(q)) {
			rcu_read_unlock();
			return false;
		}
	}
	rcu_read_unlock();
	return true;
}

/* Are any of the TX qdiscs changing?  */
static inline bool qdisc_tx_changing(const struct net_device *dev)
{
	unsigned int i;

	for (i = 0; i < dev->num_tx_queues; i++) {
		struct netdev_queue *txq = netdev_get_tx_queue(dev, i);

		if (rcu_access_pointer(txq->qdisc) !=
		    rcu_access_pointer(txq->qdisc_sleeping))
			return true;
	}
	return false;
}

/* "noqueue" qdisc identified by not having any enqueue, see noqueue_init() */
static inline bool qdisc_txq_has_no_queue(const struct netdev_queue *txq)
{
	struct Qdisc *qdisc = rcu_access_pointer(txq->qdisc);

	return qdisc->enqueue == NULL;
}

/* Is the device using the noop qdisc on all queues?  */
static inline bool qdisc_tx_is_noop(const struct net_device *dev)
{
	unsigned int i;

	for (i = 0; i < dev->num_tx_queues; i++) {
		struct netdev_queue *txq = netdev_get_tx_queue(dev, i);
		if (rcu_access_pointer(txq->qdisc) != &noop_qdisc)
			return false;
	}
	return true;
}

static inline unsigned int qdisc_pkt_len(const struct sk_buff *skb)
{
	return qdisc_skb_cb(skb)->pkt_len;
}

/* additional qdisc xmit flags (NET_XMIT_MASK in linux/netdevice.h) */
enum net_xmit_qdisc_t {
	__NET_XMIT_STOLEN = 0x00010000,
	__NET_XMIT_BYPASS = 0x00020000,
};

#ifdef CONFIG_NET_CLS_ACT
#define net_xmit_drop_count(e)	((e) & __NET_XMIT_STOLEN ? 0 : 1)
#else
#define net_xmit_drop_count(e)	(1)
#endif

static inline void qdisc_calculate_pkt_len(struct sk_buff *skb,
					   const struct Qdisc *sch)
{
#ifdef CONFIG_NET_SCHED
	struct qdisc_size_table *stab = rcu_dereference_bh(sch->stab);

	if (stab)
		__qdisc_calculate_pkt_len(skb, stab);
#endif
}

static inline int qdisc_enqueue(struct sk_buff *skb, struct Qdisc *sch,
				struct sk_buff **to_free)
{
	return sch->enqueue(skb, sch, to_free);
}

static inline void _bstats_update(struct gnet_stats_basic_sync *bstats,
				  __u64 bytes, __u64 packets)
{
	u64_stats_update_begin(&bstats->syncp);
	u64_stats_add(&bstats->bytes, bytes);
	u64_stats_add(&bstats->packets, packets);
	u64_stats_update_end(&bstats->syncp);
}

static inline void bstats_update(struct gnet_stats_basic_sync *bstats,
				 const struct sk_buff *skb)
{
	_bstats_update(bstats,
		       qdisc_pkt_len(skb),
		       skb_is_gso(skb) ? skb_shinfo(skb)->gso_segs : 1);
}

static inline void qdisc_bstats_cpu_update(struct Qdisc *sch,
					   const struct sk_buff *skb)
{
	bstats_update(this_cpu_ptr(sch->cpu_bstats), skb);
}

static inline void qdisc_bstats_update(struct Qdisc *sch,
				       const struct sk_buff *skb)
{
	bstats_update(&sch->bstats, skb);
}

static inline void qdisc_qstats_backlog_dec(struct Qdisc *sch,
					    const struct sk_buff *skb)
{
	sch->qstats.backlog -= qdisc_pkt_len(skb);
}

static inline void qdisc_qstats_cpu_backlog_dec(struct Qdisc *sch,
						const struct sk_buff *skb)
{
	this_cpu_sub(sch->cpu_qstats->backlog, qdisc_pkt_len(skb));
}

static inline void qdisc_qstats_backlog_inc(struct Qdisc *sch,
					    const struct sk_buff *skb)
{
	sch->qstats.backlog += qdisc_pkt_len(skb);
}

static inline void qdisc_qstats_cpu_backlog_inc(struct Qdisc *sch,
						const struct sk_buff *skb)
{
	this_cpu_add(sch->cpu_qstats->backlog, qdisc_pkt_len(skb));
}

static inline void qdisc_qstats_cpu_qlen_inc(struct Qdisc *sch)
{
	this_cpu_inc(sch->cpu_qstats->qlen);
}

static inline void qdisc_qstats_cpu_qlen_dec(struct Qdisc *sch)
{
	this_cpu_dec(sch->cpu_qstats->qlen);
}

static inline void qdisc_qstats_cpu_requeues_inc(struct Qdisc *sch)
{
	this_cpu_inc(sch->cpu_qstats->requeues);
}

static inline void __qdisc_qstats_drop(struct Qdisc *sch, int count)
{
	sch->qstats.drops += count;
}

static inline void qstats_drop_inc(struct gnet_stats_queue *qstats)
{
	qstats->drops++;
}

static inline void qstats_overlimit_inc(struct gnet_stats_queue *qstats)
{
	qstats->overlimits++;
}

static inline void qdisc_qstats_drop(struct Qdisc *sch)
{
	qstats_drop_inc(&sch->qstats);
}

static inline void qdisc_qstats_cpu_drop(struct Qdisc *sch)
{
	this_cpu_inc(sch->cpu_qstats->drops);
}

static inline void qdisc_qstats_overlimit(struct Qdisc *sch)
{
	sch->qstats.overlimits++;
}

static inline int qdisc_qstats_copy(struct gnet_dump *d, struct Qdisc *sch)
{
	__u32 qlen = qdisc_qlen_sum(sch);

	return gnet_stats_copy_queue(d, sch->cpu_qstats, &sch->qstats, qlen);
}

static inline void qdisc_qstats_qlen_backlog(struct Qdisc *sch,  __u32 *qlen,
					     __u32 *backlog)
{
	struct gnet_stats_queue qstats = { 0 };

	gnet_stats_add_queue(&qstats, sch->cpu_qstats, &sch->qstats);
	*qlen = qstats.qlen + qdisc_qlen(sch);
	*backlog = qstats.backlog;
}

static inline void qdisc_purge_queue(struct Qdisc *sch)
{
	__u32 qlen, backlog;

	qdisc_qstats_qlen_backlog(sch, &qlen, &backlog);
	qdisc_reset(sch);
	qdisc_tree_reduce_backlog(sch, qlen, backlog);
}

static inline void __qdisc_enqueue_tail(struct sk_buff *skb,
					struct qdisc_skb_head *qh)
{
	struct sk_buff *last = qh->tail;

	if (last) {
		skb->next = NULL;
		last->next = skb;
		qh->tail = skb;
	} else {
		qh->tail = skb;
		qh->head = skb;
	}
	qh->qlen++;
}

static inline int qdisc_enqueue_tail(struct sk_buff *skb, struct Qdisc *sch)
{
	__qdisc_enqueue_tail(skb, &sch->q);
	qdisc_qstats_backlog_inc(sch, skb);
	return NET_XMIT_SUCCESS;
}

static inline void __qdisc_enqueue_head(struct sk_buff *skb,
					struct qdisc_skb_head *qh)
{
	skb->next = qh->head;

	if (!qh->head)
		qh->tail = skb;
	qh->head = skb;
	qh->qlen++;
}

static inline struct sk_buff *__qdisc_dequeue_head(struct qdisc_skb_head *qh)
{
	struct sk_buff *skb = qh->head;

	if (likely(skb != NULL)) {
		qh->head = skb->next;
		qh->qlen--;
		if (qh->head == NULL)
			qh->tail = NULL;
		skb->next = NULL;
	}

	return skb;
}

static inline struct sk_buff *qdisc_dequeue_internal(struct Qdisc *sch, bool direct)
{
	struct sk_buff *skb;

	skb = __skb_dequeue(&sch->gso_skb);
	if (skb) {
		sch->q.qlen--;
		return skb;
	}
	if (direct)
		return __qdisc_dequeue_head(&sch->q);
	else
		return sch->dequeue(sch);
}

static inline struct sk_buff *qdisc_dequeue_head(struct Qdisc *sch)
{
	struct sk_buff *skb = __qdisc_dequeue_head(&sch->q);

	if (likely(skb != NULL)) {
		qdisc_qstats_backlog_dec(sch, skb);
		qdisc_bstats_update(sch, skb);
	}

	return skb;
}

struct tc_skb_cb {
	struct qdisc_skb_cb qdisc_cb;
	u32 drop_reason;

	u16 zone; /* Only valid if post_ct = true */
	u16 mru;
	u8 post_ct:1;
	u8 post_ct_snat:1;
	u8 post_ct_dnat:1;
};

static inline struct tc_skb_cb *tc_skb_cb(const struct sk_buff *skb)
{
	struct tc_skb_cb *cb = (struct tc_skb_cb *)skb->cb;

	BUILD_BUG_ON(sizeof(*cb) > sizeof_field(struct sk_buff, cb));
	return cb;
}

static inline enum skb_drop_reason
tcf_get_drop_reason(const struct sk_buff *skb)
{
	return tc_skb_cb(skb)->drop_reason;
}

static inline void tcf_set_drop_reason(const struct sk_buff *skb,
				       enum skb_drop_reason reason)
{
	tc_skb_cb(skb)->drop_reason = reason;
}

/* Instead of calling kfree_skb() while root qdisc lock is held,
 * queue the skb for future freeing at end of __dev_xmit_skb()
 */
static inline void __qdisc_drop(struct sk_buff *skb, struct sk_buff **to_free)
{
	skb->next = *to_free;
	*to_free = skb;
}

static inline void __qdisc_drop_all(struct sk_buff *skb,
				    struct sk_buff **to_free)
{
	if (skb->prev)
		skb->prev->next = *to_free;
	else
		skb->next = *to_free;
	*to_free = skb;
}

static inline unsigned int __qdisc_queue_drop_head(struct Qdisc *sch,
						   struct qdisc_skb_head *qh,
						   struct sk_buff **to_free)
{
	struct sk_buff *skb = __qdisc_dequeue_head(qh);

	if (likely(skb != NULL)) {
		unsigned int len = qdisc_pkt_len(skb);

		qdisc_qstats_backlog_dec(sch, skb);
		__qdisc_drop(skb, to_free);
		return len;
	}

	return 0;
}

static inline struct sk_buff *qdisc_peek_head(struct Qdisc *sch)
{
	const struct qdisc_skb_head *qh = &sch->q;

	return qh->head;
}

/* generic pseudo peek method for non-work-conserving qdisc */
static inline struct sk_buff *qdisc_peek_dequeued(struct Qdisc *sch)
{
	struct sk_buff *skb = skb_peek(&sch->gso_skb);

	/* we can reuse ->gso_skb because peek isn't called for root qdiscs */
	if (!skb) {
		skb = sch->dequeue(sch);

		if (skb) {
			__skb_queue_head(&sch->gso_skb, skb);
			/* it's still part of the queue */
			qdisc_qstats_backlog_inc(sch, skb);
			sch->q.qlen++;
		}
	}

	return skb;
}

static inline void qdisc_update_stats_at_dequeue(struct Qdisc *sch,
						 struct sk_buff *skb)
{
	if (qdisc_is_percpu_stats(sch)) {
		qdisc_qstats_cpu_backlog_dec(sch, skb);
		qdisc_bstats_cpu_update(sch, skb);
		qdisc_qstats_cpu_qlen_dec(sch);
	} else {
		qdisc_qstats_backlog_dec(sch, skb);
		qdisc_bstats_update(sch, skb);
		sch->q.qlen--;
	}
}

static inline void qdisc_update_stats_at_enqueue(struct Qdisc *sch,
						 unsigned int pkt_len)
{
	if (qdisc_is_percpu_stats(sch)) {
		qdisc_qstats_cpu_qlen_inc(sch);
		this_cpu_add(sch->cpu_qstats->backlog, pkt_len);
	} else {
		sch->qstats.backlog += pkt_len;
		sch->q.qlen++;
	}
}

/* use instead of qdisc->dequeue() for all qdiscs queried with ->peek() */
static inline struct sk_buff *qdisc_dequeue_peeked(struct Qdisc *sch)
{
	struct sk_buff *skb = skb_peek(&sch->gso_skb);

	if (skb) {
		skb = __skb_dequeue(&sch->gso_skb);
		if (qdisc_is_percpu_stats(sch)) {
			qdisc_qstats_cpu_backlog_dec(sch, skb);
			qdisc_qstats_cpu_qlen_dec(sch);
		} else {
			qdisc_qstats_backlog_dec(sch, skb);
			sch->q.qlen--;
		}
	} else {
		skb = sch->dequeue(sch);
	}

	return skb;
}

static inline void __qdisc_reset_queue(struct qdisc_skb_head *qh)
{
	/*
	 * We do not know the backlog in bytes of this list, it
	 * is up to the caller to correct it
	 */
	ASSERT_RTNL();
	if (qh->qlen) {
		rtnl_kfree_skbs(qh->head, qh->tail);

		qh->head = NULL;
		qh->tail = NULL;
		qh->qlen = 0;
	}
}

static inline void qdisc_reset_queue(struct Qdisc *sch)
{
	__qdisc_reset_queue(&sch->q);
}

static inline struct Qdisc *qdisc_replace(struct Qdisc *sch, struct Qdisc *new,
					  struct Qdisc **pold)
{
	struct Qdisc *old;

	sch_tree_lock(sch);
	old = *pold;
	*pold = new;
	if (old != NULL)
		qdisc_purge_queue(old);
	sch_tree_unlock(sch);

	return old;
}

static inline void rtnl_qdisc_drop(struct sk_buff *skb, struct Qdisc *sch)
{
	rtnl_kfree_skbs(skb, skb);
	qdisc_qstats_drop(sch);
}

static inline int qdisc_drop_cpu(struct sk_buff *skb, struct Qdisc *sch,
				 struct sk_buff **to_free)
{
	__qdisc_drop(skb, to_free);
	qdisc_qstats_cpu_drop(sch);

	return NET_XMIT_DROP;
}

static inline int qdisc_drop(struct sk_buff *skb, struct Qdisc *sch,
			     struct sk_buff **to_free)
{
	__qdisc_drop(skb, to_free);
	qdisc_qstats_drop(sch);

	return NET_XMIT_DROP;
}

static inline int qdisc_drop_reason(struct sk_buff *skb, struct Qdisc *sch,
				    struct sk_buff **to_free,
				    enum skb_drop_reason reason)
{
	tcf_set_drop_reason(skb, reason);
	return qdisc_drop(skb, sch, to_free);
}

static inline int qdisc_drop_all(struct sk_buff *skb, struct Qdisc *sch,
				 struct sk_buff **to_free)
{
	__qdisc_drop_all(skb, to_free);
	qdisc_qstats_drop(sch);

	return NET_XMIT_DROP;
}

struct psched_ratecfg {
	u64	rate_bytes_ps; /* bytes per second */
	u32	mult;
	u16	overhead;
	u16	mpu;
	u8	linklayer;
	u8	shift;
};

static inline u64 psched_l2t_ns(const struct psched_ratecfg *r,
				unsigned int len)
{
	len += r->overhead;

	if (len < r->mpu)
		len = r->mpu;

	if (unlikely(r->linklayer == TC_LINKLAYER_ATM))
		return ((u64)(DIV_ROUND_UP(len,48)*53) * r->mult) >> r->shift;

	return ((u64)len * r->mult) >> r->shift;
}

void psched_ratecfg_precompute(struct psched_ratecfg *r,
			       const struct tc_ratespec *conf,
			       u64 rate64);

static inline void psched_ratecfg_getrate(struct tc_ratespec *res,
					  const struct psched_ratecfg *r)
{
	memset(res, 0, sizeof(*res));

	/* legacy struct tc_ratespec has a 32bit @rate field
	 * Qdisc using 64bit rate should add new attributes
	 * in order to maintain compatibility.
	 */
	res->rate = min_t(u64, r->rate_bytes_ps, ~0U);

	res->overhead = r->overhead;
	res->mpu = r->mpu;
	res->linklayer = (r->linklayer & TC_LINKLAYER_MASK);
}

struct psched_pktrate {
	u64	rate_pkts_ps; /* packets per second */
	u32	mult;
	u8	shift;
};

static inline u64 psched_pkt2t_ns(const struct psched_pktrate *r,
				  unsigned int pkt_num)
{
	return ((u64)pkt_num * r->mult) >> r->shift;
}

void psched_ppscfg_precompute(struct psched_pktrate *r, u64 pktrate64);

/* Mini Qdisc serves for specific needs of ingress/clsact Qdisc.
 * The fast path only needs to access filter list and to update stats
 */
struct mini_Qdisc {
	struct tcf_proto *filter_list;
	struct tcf_block *block;
	struct gnet_stats_basic_sync __percpu *cpu_bstats;
	struct gnet_stats_queue	__percpu *cpu_qstats;
	unsigned long rcu_state;
};

static inline void mini_qdisc_bstats_cpu_update(struct mini_Qdisc *miniq,
						const struct sk_buff *skb)
{
	bstats_update(this_cpu_ptr(miniq->cpu_bstats), skb);
}

static inline void mini_qdisc_qstats_cpu_drop(struct mini_Qdisc *miniq)
{
	this_cpu_inc(miniq->cpu_qstats->drops);
}

struct mini_Qdisc_pair {
	struct mini_Qdisc miniq1;
	struct mini_Qdisc miniq2;
	struct mini_Qdisc __rcu **p_miniq;
};

void mini_qdisc_pair_swap(struct mini_Qdisc_pair *miniqp,
			  struct tcf_proto *tp_head);
void mini_qdisc_pair_init(struct mini_Qdisc_pair *miniqp, struct Qdisc *qdisc,
			  struct mini_Qdisc __rcu **p_miniq);
void mini_qdisc_pair_block_init(struct mini_Qdisc_pair *miniqp,
				struct tcf_block *block);

void mq_change_real_num_tx(struct Qdisc *sch, unsigned int new_real_tx);

int sch_frag_xmit_hook(struct sk_buff *skb, int (*xmit)(struct sk_buff *skb));

/* Make sure qdisc is no longer in SCHED state. */
static inline void qdisc_synchronize(const struct Qdisc *q)
{
	while (test_bit(__QDISC_STATE_SCHED, &q->state))
		msleep(1);
}

#endif
