// clang-format off
#include "types.h"
#include "date.h"
#include "defs.h"
#include "exo.h"
#include <string.h>
#include "fs.h"
#include "sleeplock.h"
#include "buf.h"
#include "memlayout.h"
#include "mmu.h"
#include "param.h"
#include "cap.h"
#include "proc.h"
#include "kernel/hypervisor/hypervisor.h"
#include "spinlock.h"
#include "x86.h"
// clang-format on

extern int exo_ipc_queue_recv_timed(exo_cap src, void *buf, uint64_t len,
                                    unsigned timeout);

int sys_fork(void) { return fork(); }

int sys_exit(void) {
  exit();
  return 0; // not reached
}

int sys_wait(void) { return wait(); }

int sys_kill(void) {
  int pid;

  if (argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int sys_getpid(void) { return myproc()->pid; }

int sys_sbrk(void) {
  int addr;
  int n;

  if (argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if (growproc(n) < 0)
    return -1;
  return addr;
}

int sys_sleep(void) {
  int n;
  uint32_t ticks0;

  if (argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n) {
    if (myproc()->killed) {
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int sys_uptime(void) {
  uint32_t xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

int sys_mappte(void) {
  int va, pa, perm;

  if (argint(0, &va) < 0 || argint(1, &pa) < 0 || argint(2, &perm) < 0)
    return -1;
  return insert_pte(myproc()->pgdir, (void *)va, pa, perm);
}

int sys_set_timer_upcall(void) {
  void (*handler)(void);
  if (argptr(0, (char **)&handler, sizeof(handler)) < 0)
    return -1;
  myproc()->timer_upcall = handler;
  return 0;
}

// allocate a physical page and return its capability
int sys_exo_alloc_page(void) {
  exo_cap *ucap;
  if (argptr(0, (void *)&ucap, sizeof(*ucap)) < 0)
    return -1;
  exo_cap cap = exo_alloc_page();
  memcpy(ucap, &cap, sizeof(cap));
  return 0;
}

// unbind and free a physical page by capability
int sys_exo_unbind_page(void) {
  exo_cap cap;
  if (argptr(0, (void *)&cap, sizeof(cap)) < 0)
    return -1;
  if (!cap_verify(cap))
    return -1;
  return exo_unbind_page(cap);
}

int sys_exo_alloc_block(void) {
  int dev, rights;
  struct exo_blockcap *ucap;
  struct exo_blockcap cap;
  if (argint(0, &dev) < 0 || argint(1, &rights) < 0 ||
      argptr(2, (void *)&ucap, sizeof(*ucap)) < 0)
    return -1;
  cap = exo_alloc_block(dev, rights);
  ucap->dev = cap.dev;
  ucap->blockno = cap.blockno;
  ucap->rights = cap.rights;
  ucap->owner = cap.owner;
  return 0;
}

int sys_exo_alloc_ioport(void) {
  int port;
  exo_cap *ucap;
  if (argint(0, &port) < 0 || argptr(1, (void *)&ucap, sizeof(*ucap)) < 0)
    return -1;
  exo_cap cap = exo_alloc_ioport((uint32_t)port);
  memcpy(ucap, &cap, sizeof(cap));
  return 0;
}

int sys_exo_bind_irq(void) {
  int irq;
  exo_cap *ucap;
  if (argint(0, &irq) < 0 || argptr(1, (void *)&ucap, sizeof(*ucap)) < 0)
    return -1;
  exo_cap cap = exo_bind_irq((uint32_t)irq);
  memcpy(ucap, &cap, sizeof(cap));
  return 0;
}

int sys_exo_alloc_dma(void) {
  int chan;
  exo_cap *ucap;
  if (argint(0, &chan) < 0 || argptr(1, (void *)&ucap, sizeof(*ucap)) < 0)
    return -1;
  exo_cap cap = exo_alloc_dma((uint32_t)chan);
  memcpy(ucap, &cap, sizeof(cap));
  return 0;
}


int sys_exo_bind_block(void) {
  struct exo_blockcap *ucap, cap;
  char *data;
  int write;
  struct buf b;

  if (argptr(0, (void *)&ucap, sizeof(cap)) < 0 ||
      argptr(1, &data, BSIZE) < 0 || argint(2, &write) < 0)
    return -1;

  cap = *ucap;
  memset(&b, 0, sizeof(b));
  initsleeplock(&b.lock, "exoblk");
  acquiresleep(&b.lock);
  if (write)
    memcpy(b.data, data, BSIZE);
  int r = exo_bind_block(&cap, &b, write);
  if (!write)
    memcpy(data, b.data, BSIZE);
  releasesleep(&b.lock);
  return r;
}

int sys_exo_flush_block(void) {
  struct exo_blockcap *ucap, cap;
  char *data;
  struct buf b;

  if (argptr(0, (void *)&ucap, sizeof(cap)) < 0 || argptr(1, &data, BSIZE) < 0)
    return -1;

  cap = *ucap;
  memset(&b, 0, sizeof(b));
  initsleeplock(&b.lock, "exoflush");
  acquiresleep(&b.lock);
  memcpy(b.data, data, BSIZE);
  int r = exo_bind_block(&cap, &b, 1);
  releasesleep(&b.lock);
  return r;
}

int sys_exo_yield_to(void) {
  exo_cap *ucap, cap;
  if (argptr(0, (void *)&ucap, sizeof(cap)) < 0)
    return -1;
  memcpy(&cap, ucap, sizeof(cap));
  if (!cap_verify(cap))
    return -1;
  return exo_yield_to(cap);
}

int sys_exo_read_disk(void) {
  struct exo_blockcap cap;
  char *dst;
  uint32_t off, n;

  if (argptr(0, (void *)&cap, sizeof(cap)) < 0 || argint(2, (int *)&off) < 0 ||
      argint(3, (int *)&n) < 0 || argptr(1, &dst, n) < 0)

    return -1;
  return exo_read_disk(cap, dst, off, n);
}

int sys_exo_write_disk(void) {
  struct exo_blockcap cap;
  char *src;
  uint32_t off, n;

  if (argptr(0, (void *)&cap, sizeof(cap)) < 0 || argint(2, (int *)&off) < 0 ||
      argint(3, (int *)&n) < 0 || argptr(1, &src, n) < 0)

    return -1;
  return exo_write_disk(cap, src, off, n);
}

int sys_exo_alloc_ioport(void) {
  int port;
  exo_cap *ucap;
  if (argint(0, &port) < 0 || argptr(1, (void *)&ucap, sizeof(*ucap)) < 0)
    return -1;
  exo_cap cap = exo_alloc_ioport((uint32_t)port);
  memcpy(ucap, &cap, sizeof(cap));
  return 0;
}

int sys_exo_bind_irq(void) {
  int irq;
  exo_cap *ucap;
  if (argint(0, &irq) < 0 || argptr(1, (void *)&ucap, sizeof(*ucap)) < 0)
    return -1;
  exo_cap cap = exo_bind_irq((uint32_t)irq);
  memcpy(ucap, &cap, sizeof(cap));
  return 0;
}

int sys_exo_alloc_dma(void) {
  exo_cap *ucap;
  if (argptr(0, (void *)&ucap, sizeof(*ucap)) < 0)
    return -1;
  exo_cap cap = exo_alloc_dma();
  memcpy(ucap, &cap, sizeof(cap));
  return 0;
}

int sys_exo_alloc_hypervisor(void) {
  exo_cap *ucap;
  if (argptr(0, (void *)&ucap, sizeof(*ucap)) < 0)
    return -1;
  exo_cap cap = exo_alloc_hypervisor();
  memcpy(ucap, &cap, sizeof(cap));
  return 0;
}

int sys_hv_launch(void) {
  exo_cap cap;
  char *path;
  if (argptr(0, (void *)&cap, sizeof(cap)) < 0 || argstr(1, &path) < 0)
    return -1;
  return hv_launch_guest(cap, path);
}

int sys_exo_send(void) {
  exo_cap *ucap, cap;
  char *src;
  uint32_t n;
  if (argptr(0, (void *)&ucap, sizeof(cap)) < 0 || argint(2, (int *)&n) < 0 ||
      argptr(1, &src, n) < 0)
    return -1;
  memcpy(&cap, ucap, sizeof(cap));
  if (!cap_verify(cap))
    return -1;
  return exo_send(cap, src, n);
}

int sys_exo_recv(void) {
  exo_cap *ucap, cap;
  char *dst;
  uint32_t n;
  if (argptr(0, (void *)&ucap, sizeof(cap)) < 0 || argint(2, (int *)&n) < 0 ||
      argptr(1, &dst, n) < 0)
    return -1;
  memcpy(&cap, ucap, sizeof(cap));
  if (!cap_verify(cap))
    return -1;
  return exo_recv(cap, dst, n);
}

int sys_exo_recv_timed(void) {
  exo_cap *ucap, cap;
  char *dst;
  uint32_t n, to;
  if (argptr(0, (void *)&ucap, sizeof(cap)) < 0 ||
      argint(2, (int *)&n) < 0 || argptr(1, &dst, n) < 0 ||
      argint(3, (int *)&to) < 0)
    return -1;
  memcpy(&cap, ucap, sizeof(cap));
  if (!cap_verify(cap))
    return -1;
  return exo_ipc_queue_recv_timed(cap, dst, n, to);
}

int sys_proc_alloc(void) {
  exo_cap *ucap;
  struct proc *np;
  struct proc *curproc = myproc();
  int i;

  if (argptr(0, (void *)&ucap, sizeof(*ucap)) < 0)
    return -1;

  if ((np = allocproc()) == 0)
    return -1;

  if ((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0) {
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;
#ifndef __x86_64__
  np->tf->eax = 0;
#else
  np->tf->rax = 0;
#endif

  for (i = 0; i < NOFILE; i++)
    if (curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  acquire(&ptable.lock);
  np->state = RUNNABLE;
  release(&ptable.lock);

  exo_cap cap = cap_new(V2P(np->context), 0, np->pid);
  *ucap = cap;
#ifdef __x86_64__
  return *(uint64_t *)&cap;
#else
  return cap.pa;
#endif
}

int sys_set_numa_node(void) {
  int node;
  if (argint(0, &node) < 0)
    return -1;
  myproc()->preferred_node = node;
  return 0;
}

int sys_set_gas(void) {
  uint64_t amount;
  if (argint(0, (int *)&amount) < 0)
    return -1;
  myproc()->gas_remaining = amount;
  if (amount > 0)
    myproc()->out_of_gas = 0;
  return 0;
}

int sys_get_gas(void) { return (int)myproc()->gas_remaining; }

int sys_sigsend(void) {
  int pid, sig;
  if (argint(0, &pid) < 0 || argint(1, &sig) < 0)
    return -1;
  return sigsend(pid, sig);
}

int sys_sigcheck(void) {
  int s = myproc()->pending_signal;
  myproc()->pending_signal = 0;
  return s;
}

int sys_cap_inc(void) {
  int id;
  if (argint(0, &id) < 0)
    return -1;
  cap_table_inc((uint32_t)id);
  return 0;
}

int sys_cap_dec(void) {
  int id;
  if (argint(0, &id) < 0)
    return -1;
  cap_table_dec((uint32_t)id);
  return 0;
}

int sys_exo_irq_alloc(void) {
  int irq, rights;
  exo_cap *ucap;
  if (argint(0, &irq) < 0 || argint(1, &rights) < 0 ||
      argptr(2, (void *)&ucap, sizeof(*ucap)) < 0)
    return -1;
  exo_cap cap = exo_alloc_irq((uint32_t)irq, (uint32_t)rights);
  memcpy(ucap, &cap, sizeof(cap));
  return 0;
}

int sys_exo_irq_wait(void) {
  exo_cap cap;
  uint32_t *irq_out;
  if (argptr(0, (void *)&cap, sizeof(cap)) < 0 ||
      argptr(1, (void *)&irq_out, sizeof(*irq_out)) < 0)
    return -1;
  return exo_irq_wait(cap, irq_out);
}

int sys_exo_irq_ack(void) {
  exo_cap cap;
  if (argptr(0, (void *)&cap, sizeof(cap)) < 0)
    return -1;
  return exo_irq_ack(cap);
}

// Provided by fastipc.c
