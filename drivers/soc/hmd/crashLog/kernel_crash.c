/*

store kernel panic info to sharememory.

*/
#include <linux/module.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/kallsyms.h>
#include <linux/soc/qcom/smem.h>
#include <linux/soc/qcom/smem_state.h>
#include <linux/sched.h>
#include <asm/stacktrace.h>
#include <linux/kern_levels.h>

#include "smem_type.h"
#include <trace/hooks/fault.h>
#include <trace/hooks/traps.h>
#include <linux/notifier.h>
#include <linux/panic_notifier.h>
#include <generated/compile.h>
#include <generated/utsrelease.h>
	
// increase crash buffer for store linux banner&subsys crash reason zzw 20200528
#define CRASH_BUFFER_SIZE		1024+256

static struct pt_regs panic_pt_regs;
static struct pt_regs *panic_pt_regs_ptr = NULL;
static char crash_message[CRASH_BUFFER_SIZE] = {0};
bool fatal_reboot_state = false;

const char carsh_linux_banner[] = "Linux version ";  	
/*

store crash log to sharememory 
(now use SMEM_RESERVED_RESET_LOG, for sm7250. other platform may need change)

*/
void store2smem(void)
{
	int ret;
	unsigned int *pBuffer = NULL;
	size_t size = min(strlen(crash_message) + 1, sizeof(crash_message));
	
	ret = qcom_smem_alloc(QCOM_SMEM_HOST_ANY, SMEM_RESERVED_RESET_LOG, size);
	if(ret < 0)
	{
		pr_err("Failed to alloc SMEM_RESERVED_RESET_LOG  sharememory\n");
        return;
	}
	
	pBuffer = qcom_smem_get(QCOM_SMEM_HOST_ANY, SMEM_RESERVED_RESET_LOG, &size);
    if (IS_ERR(pBuffer)) {
        pr_err("Failed to acquire SMEM_RESERVED_RESET_LOG entry\n");
        return;
    }
    if(fatal_reboot_state == true)
    {
        strcpy((char *)pBuffer, "FatalReboot:");    	
    }
    else
    {		
	      strcpy((char *)pBuffer, "CRASH:");
	  }
    memcpy((char *)pBuffer+strlen((char *)pBuffer), crash_message, size);
}


void store_crash_message(const char *fmt, ...) {
    size_t size;
    va_list args;

	size = sizeof(crash_message) - strlen(crash_message);
    if (size <= 0)
        return;

    va_start(args, fmt);
    vsnprintf(crash_message + strlen(crash_message), size, fmt, args);
    va_end(args);
}


void dump_kernel_fault(const char *desc, unsigned long addr) 
{
    store_crash_message("Unable to handle kernel %s at virtual address %016lx\n", desc, addr);
}


void dump_kernel_die(const char *desc, int err, struct pt_regs *regs) 
{
    store_crash_message("Internal error: %s: %x\n", desc, err);
    memcpy(&panic_pt_regs, regs, sizeof(struct pt_regs));
    panic_pt_regs_ptr = &panic_pt_regs;
}

void dump_kernel_serror( int err, struct pt_regs *regs) 
{
    store_crash_message("Internal error: %s: %x\n", err);
    memcpy(&panic_pt_regs, regs, sizeof(struct pt_regs));
    panic_pt_regs_ptr = &panic_pt_regs;
}


//zhongli.liu add  start
/*
 * Unwind from one frame record (A) to the next frame record (B).
 *
 * We terminate early if the location of B indicates a malformed chain of frame
 * records (e.g. a cycle), determined based on the location and fp value of A
 * and the location (but not the fp value) of B.
 */
int notrace crash_unwind_frame(struct task_struct *tsk, struct stackframe *frame)
{
	unsigned long fp = frame->fp;
	struct stack_info info;

	if (!tsk)
		tsk = current;

	/* Final frame; nothing to unwind */
	if (fp == (unsigned long)task_pt_regs(tsk)->stackframe)
		return -ENOENT;

	if (fp & 0x7)
		return -EINVAL;

//	if (!on_accessible_stack(tsk, fp, 16, &info))
//		return -EINVAL;

	if (test_bit(info.type, frame->stacks_done))
		return -EINVAL;

	/*
	 * As stacks grow downward, any valid record on the same stack must be
	 * at a strictly higher address than the prior record.
	 *
	 * Stacks can nest in several valid orders, e.g.
	 *
	 * TASK -> IRQ -> OVERFLOW -> SDEI_NORMAL
	 * TASK -> SDEI_NORMAL -> SDEI_CRITICAL -> OVERFLOW
	 *
	 * ... but the nesting itself is strict. Once we transition from one
	 * stack to another, it's never valid to unwind back to that first
	 * stack.
	 */
	if (info.type == frame->prev_type) {
		if (fp <= frame->prev_fp)
			return -EINVAL;
	} else {
		set_bit(frame->prev_type, frame->stacks_done);
	}

	/*
	 * Record this frame record's values and location. The prev_fp and
	 * prev_type are only meaningful to the next unwind_frame() invocation.
	 */
	frame->fp = READ_ONCE_NOCHECK(*(unsigned long *)(fp));
	frame->pc = READ_ONCE_NOCHECK(*(unsigned long *)(fp + 8));
	frame->prev_fp = fp;
	frame->prev_type = info.type;

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	if (tsk->ret_stack &&
		(ptrauth_strip_insn_pac(frame->pc) == (unsigned long)return_to_handler)) {
		struct ftrace_ret_stack *ret_stack;
		/*
		 * This is a case where function graph tracer has
		 * modified a return address (LR) in a stack frame
		 * to hook a function return.
		 * So replace it to an original value.
		 */
		ret_stack = ftrace_graph_get_ret_stack(tsk, frame->graph++);
		if (WARN_ON_ONCE(!ret_stack))
			return -EINVAL;
		frame->pc = ret_stack->ret;
	}
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */

	frame->pc = ptrauth_strip_insn_pac(frame->pc);

	return 0;
}
//zhongli.liu add  end

// store subsystem fail reason to RESET LOG buffer zzw 20200528 begin
void dump_subsys_fault_reason(const char *desc, const char * reason)
{
    store_crash_message("%s subsystem failure reason: %s.\n", desc, reason);
}
// store subsystem fail reason to RESET LOG buffer zzw 20200528 end

void store_kernel_panic_smem(const char *desc) {
    struct stackframe frame;
    struct task_struct *tsk = current;
    struct pt_regs *regs = panic_pt_regs_ptr;
    int skip = 0;

	//add by changxue.fang for ramdump get sw version,20201224,start
	store_crash_message("software version: %s\n", HMD_SW_VERSION);
	//add by changxue.fang for ramdump get sw version,20201224,end

    // store linux banner to RESET LOG zzw 20200528 add
   store_crash_message("linux banner: %s ", carsh_linux_banner); 
   store_crash_message("%s ", UTS_RELEASE); 
   store_crash_message("( %s ", LINUX_COMPILE_BY); 
   store_crash_message("@ %s ) ", LINUX_COMPILE_HOST); 
   store_crash_message("( %s ) %s\n", LINUX_COMPILER,UTS_VERSION); 

   	          
    if(fatal_reboot_state == true)
    {
        store_crash_message("FatalReboot: %s\n", desc);	
    }
    else
    {		
        //dump_backtrace(regs, tsk, KERN_DEFAULT);    		     	
		    store_crash_message("Kernel panic - not syncing: %s\n", desc);
		    store_crash_message("CPU: %d PID: %d Comm: %.20s\n",
		            raw_smp_processor_id(), tsk->pid, tsk->comm);
		
		    if (regs != NULL) {
				store_crash_message("pc : %pS\n", (void *)regs->pc);
		#ifdef CONFIG_ARM64
				store_crash_message("lr : %pS\n", (void *)regs->regs[30]);
		#endif
		        store_crash_message("sp : %016llx\n", regs->sp);
		        
				skip = 1;
		    }
		
		    if (!try_get_task_stack(tsk))
		        return;
		        

		    frame.fp = (unsigned long)__builtin_frame_address(0);
		    frame.pc = (unsigned long)store_kernel_panic_smem;
		
		    store_crash_message("CALL TRACK:\n");
  
		    do {
		        if (!skip) {
		            store_crash_message(" %pS\n", (void *)frame.pc);
		        } else if (frame.fp == frame_pointer(regs)) {
		            skip = 0;
		            store_crash_message(" %pS\n", (void *)regs->pc);
		        }
    		        
		    } while (!crash_unwind_frame(tsk, &frame));

		   /// put_task_stack(tsk);
    }
    store2smem();

    panic_pt_regs_ptr = NULL;
}

void dump_set_fatal_reboot(const char *desc, const char * reason)
{
    fatal_reboot_state = true;
}

static void android_rvh_die_kernel_fault(void *unused,const char *msg, unsigned long addr,
			     unsigned int esr, struct pt_regs *regs)
{
    dump_kernel_fault(msg, addr);
}			     

static void android_rvh_do_serror(void *unused,struct pt_regs *regs, unsigned int esr, int *ret)
{
    dump_kernel_serror(esr,regs);
}			 

static int panic_save_dump_reason(struct notifier_block *this, unsigned long event, void *buf)
{

  store_kernel_panic_smem(buf);
	return NOTIFY_DONE;
}

static struct notifier_block panic_block = {
	.notifier_call = panic_save_dump_reason,
};

     
static int __init carsh_reason_init(void)
{
	int ret = 0;
	
	register_trace_android_rvh_die_kernel_fault(android_rvh_die_kernel_fault, NULL);
  register_trace_android_rvh_do_serror(android_rvh_do_serror, NULL);
	atomic_notifier_chain_register(&panic_notifier_list, &panic_block);
	
	return ret;
}

early_initcall(carsh_reason_init);
MODULE_LICENSE("GPL v2");
