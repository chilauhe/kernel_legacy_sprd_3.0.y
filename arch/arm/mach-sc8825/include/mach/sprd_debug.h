#ifndef SPRD_DEBUG_H
#define SPRD_DEBUG_H

#include <linux/sched.h>
#include <linux/semaphore.h>



#if defined(CONFIG_SPRD_DEBUG)

#define MAGIC_ADDR			(SPRD_IRAM_BASE + 0x00003f64)
#define SPRD_INFORM0			(MAGIC_ADDR + 0x00000004)
#define SPRD_INFORM1			(SPRD_INFORM0 + 0x00000004)
#define SPRD_INFORM2			(SPRD_INFORM1 + 0x00000004)
#define SPRD_INFORM3			(SPRD_INFORM2 + 0x00000004)
#define SPRD_INFORM4			(SPRD_INFORM3 + 0x00000004)
#define SPRD_INFORM5			(SPRD_INFORM4 + 0x00000004)
#define SPRD_INFORM6			(SPRD_INFORM5 + 0x00000004)

union sprd_debug_level_t {
	struct {
		u16 kernel_fault;
		u16 user_fault;
	} en;
	u32 uint_val;
};

extern union sprd_debug_level_t sprd_debug_level;

extern int sprd_debug_init(void);

extern void sprd_debug_check_crash_key(unsigned int code, int value);

extern void sprd_getlog_supply_fbinfo(void *p_fb, u32 res_x, u32 res_y, u32 bpp,
				     u32 frames);
extern void sprd_getlog_supply_loggerinfo(void *p_main, void *p_radio,
					 void *p_events, void *p_system);
extern void sprd_getlog_supply_kloginfo(void *klog_buf);

extern void sprd_gaf_supply_rqinfo(unsigned short curr_offset,
				  unsigned short rq_offset);

extern void sprd_debug_save_pte(void *pte, int task_addr); 

#else
static inline int sprd_debug_init(void)
{
	return 0;
}

static inline void sprd_debug_check_crash_key(unsigned int code, int value)
{
}

static inline void sprd_getlog_supply_fbinfo(void *p_fb, u32 res_x, u32 res_y,
					    u32 bpp, u32 frames)
{
}

static inline void sprd_getlog_supply_meminfo(u32 size0, u32 addr0, u32 size1,
					     u32 addr1)
{
}

static inline void sprd_getlog_supply_loggerinfo(void *p_main,
						void *p_radio, void *p_events,
						void *p_system)
{
}

static inline void sprd_getlog_supply_kloginfo(void *klog_buf)
{
}

static inline void sprd_gaf_supply_rqinfo(unsigned short curr_offset,
					 unsigned short rq_offset)
{
}

void sprd_debug_save_pte(void *pte, unsigned int faulttype );  
{
}

#endif

struct worker;
struct work_struct;

#ifdef CONFIG_SPRD_DEBUG_SCHED_LOG
extern void __sprd_debug_task_log(int cpu, struct task_struct *task);
extern void __sprd_debug_irq_log(unsigned int irq, void *fn, int en);
extern void __sprd_debug_work_log(struct worker *worker,
				 struct work_struct *work, work_func_t f);
extern void __sprd_debug_hrtimer_log(struct hrtimer *timer,
			enum hrtimer_restart (*fn) (struct hrtimer *), int en);

static inline void sprd_debug_task_log(int cpu, struct task_struct *task)
{
	if (unlikely(sprd_debug_level.en.kernel_fault))
		__sprd_debug_task_log(cpu, task);
}

static inline void sprd_debug_irq_log(unsigned int irq, void *fn, int en)
{
	if (unlikely(sprd_debug_level.en.kernel_fault))
		__sprd_debug_irq_log(irq, fn, en);
}

static inline void sprd_debug_work_log(struct worker *worker,
				      struct work_struct *work, work_func_t f)
{
	if (unlikely(sprd_debug_level.en.kernel_fault))
		__sprd_debug_work_log(worker, work, f);
}

static inline void sprd_debug_hrtimer_log(struct hrtimer *timer,
			 enum hrtimer_restart (*fn) (struct hrtimer *), int en)
{
#ifdef CONFIG_SPRD_DEBUG_HRTIMER_LOG
	if (unlikely(sprd_debug_level.en.kernel_fault))
		__sprd_debug_hrtimer_log(timer, fn, en);
#endif
}

static inline void sprd_debug_softirq_log(unsigned int irq, void *fn, int en)
{
#ifdef CONFIG_SPRD_DEBUG_SOFTIRQ_LOG
	if (unlikely(sprd_debug_level.en.kernel_fault))
		__sprd_debug_irq_log(irq, fn, en);
#endif
}

#else

static inline void sprd_debug_task_log(int cpu, struct task_struct *task)
{
}

static inline void sprd_debug_irq_log(unsigned int irq, void *fn, int en)
{
}

static inline void sprd_debug_work_log(struct worker *worker,
				      struct work_struct *work, work_func_t f)
{
}

static inline void sprd_debug_hrtimer_log(struct hrtimer *timer,
			 enum hrtimer_restart (*fn) (struct hrtimer *), int en)
{
}

static inline void sprd_debug_softirq_log(unsigned int irq, void *fn, int en)
{
}
#endif

#ifdef CONFIG_SPRD_DEBUG_IRQ_EXIT_LOG
extern void sprd_debug_irq_last_exit_log(void);
#else
static inline void sprd_debug_irq_last_exit_log(void)
{
}
#endif

#ifdef CONFIG_SPRD_DEBUG_SEMAPHORE_LOG
extern void debug_semaphore_init(void);
extern void debug_semaphore_down_log(struct semaphore *sem);
extern void debug_semaphore_up_log(struct semaphore *sem);
extern void debug_rwsemaphore_init(void);
extern void debug_rwsemaphore_down_log(struct rw_semaphore *sem, int dir);
extern void debug_rwsemaphore_up_log(struct rw_semaphore *sem);
#define debug_rwsemaphore_down_read_log(x) \
	debug_rwsemaphore_down_log(x,READ_SEM)
#define debug_rwsemaphore_down_write_log(x) \
	debug_rwsemaphore_down_log(x,WRITE_SEM)
#else
static inline void debug_semaphore_init(void)
{
}

static inline void debug_semaphore_down_log(struct semaphore *sem)
{
}

static inline void debug_semaphore_up_log(struct semaphore *sem)
{
}

static inline void debug_rwsemaphore_init(void)
{
}

static inline void debug_rwsemaphore_down_read_log(struct rw_semaphore *sem)
{
}

static inline void debug_rwsemaphore_down_write_log(struct rw_semaphore *sem)
{
}

static inline void debug_rwsemaphore_up_log(struct rw_semaphore *sem)
{
}
#endif

/* klaatu - schedule log */
#ifdef CONFIG_SPRD_DEBUG_SCHED_LOG
#define SCHED_LOG_MAX 2048 
#define CP_DEBUG

struct sched_log {
	struct task_log {
		unsigned long long time;
#ifdef CP_DEBUG
		unsigned long sys_cnt;
#endif
		char comm[TASK_COMM_LEN];
		pid_t pid;
	} task[NR_CPUS][SCHED_LOG_MAX];
	struct irq_log {
		unsigned long long time;
#ifdef CP_DEBUG
		unsigned long sys_cnt;
#endif
		int irq;
		void *fn;
		int en;
	} irq[NR_CPUS][SCHED_LOG_MAX];
	struct work_log {
		unsigned long long time;
#ifdef CP_DEBUG
		unsigned long sys_cnt;
#endif
		struct worker *worker;
		struct work_struct *work;
		work_func_t f;
	} work[NR_CPUS][SCHED_LOG_MAX];
	struct hrtimer_log {
		unsigned long long time;
#ifdef CP_DEBUG
		unsigned long sys_cnt;
#endif
		struct hrtimer *timer;
		enum hrtimer_restart (*fn)(struct hrtimer *);
		int en;
	} hrtimers[NR_CPUS][8];
};
#endif				/* CONFIG_SPRD_DEBUG_SCHED_LOG */

#ifdef CONFIG_SPRD_DEBUG_SEMAPHORE_LOG
#define SEMAPHORE_LOG_MAX 100
struct sem_debug{
	struct list_head list;
	struct semaphore *sem;
	struct task_struct *task;
	pid_t pid;
	int cpu;
	/* char comm[TASK_COMM_LEN]; */
};

enum {
	READ_SEM,
	WRITE_SEM
};

#define RWSEMAPHORE_LOG_MAX 100
struct rwsem_debug{
	struct list_head list;
	struct rw_semaphore *sem;
	struct task_struct *task;
	pid_t pid;
	int cpu;
	int direction;
	/* char comm[TASK_COMM_LEN]; */
};

#endif /* CONFIG_SPRD_DEBUG_SEMAPHORE_LOG */

#endif /* SPRD_DEBUG_H */
