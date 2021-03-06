#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"



// Debug variables, true causes extra printouts to consule
#define DBP false
#define DBP_DUMP false
#define MAX_ARGS 50
#define MAX_FILE_NAME_LEN 15

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp, char **argv, int argc);


/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *cmd_line) 
{
  char *save_ptr, *cmd_copy1; // copy1 (of cmd_line) is to process file name
  char *cmd_copy2; // copy2 (of cmd_line) is sent to the new thread
  char *file_name; // the first arg of cmd_line
  tid_t tid;
  struct thread *cur = thread_current();

  /* Make 2 copies of FILE_NAME.
     1 to prevent a race between the caller and load(). 
     2 to prevent changes to constant identifier cmd_line*/
  
  cmd_copy1 = palloc_get_page (0);
  cmd_copy2 = palloc_get_page (0);
  if (cmd_copy1 == NULL || cmd_copy2 == NULL)
    return TID_ERROR;

  /* Make copies and extract the file_name */
  strlcpy (cmd_copy1, cmd_line, PGSIZE);
  strlcpy (cmd_copy2, cmd_line, PGSIZE);
  file_name = strtok_r (cmd_copy1, " ", &save_ptr);    

  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (file_name, PRI_DEFAULT, start_process, cmd_copy2);
  
  /* Deallocate command line copies and return if error */
  palloc_free_page (cmd_copy1);
  if (tid == TID_ERROR)
  {
    palloc_free_page (cmd_copy2); 
    return TID_ERROR;
  }  
  
  /* add child tid_status node to child_nodes list, after child thread loads */
  struct thread *child = get_thread_by_tid(tid);
  if(child == NULL) 
     return TID_ERROR;

  struct tid_status *child_node = child->tid_node;
  sema_down(&child->load_sema); // wait for the child to finish trying to load
  if(child_node->loaded == 1) // add node only if thread completed loading
     list_push_back(&cur->child_nodes, &child_node->elem); 
  else
     return TID_ERROR;

  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *cmd_line)
{
  /* variables for argument parsing */
  char *token, *string_copy, *argv[MAX_ARGS];
  
  /* variables for starting process */
  struct intr_frame if_;
  bool success;
  char file_name[MAX_FILE_NAME_LEN];
 

  /* perform argument parsing, setting each argument on to argv*/
  int argc = 0;
  for (token = strtok_r (cmd_line, " ", &string_copy); token != NULL;
       token = strtok_r (NULL, " ", &string_copy))
  {
    argv[argc++] = token;
  }
  argv[argc] = NULL;
  strlcpy(file_name, argv[0], MAX_FILE_NAME_LEN);

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (file_name, &if_.eip, &if_.esp, argv, argc);
  palloc_free_page (cmd_line);
 
  /* If load succeeds, set loaded variable */
  struct thread *cur = thread_current();
  if(success)
     cur->tid_node->loaded = 1; 

  /* Signal to parent that the thread has completed loading, or failed. */  
  sema_up(&cur->load_sema);

  /* If load failed, quit. */
  if (!success) 
    thread_exit ();

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid) 
{
  struct thread *cur = thread_current();
  struct list *child_list = &cur->child_nodes;
  struct tid_status *child_tid_node;

  /* Verify existance of child node.
   * If list is empty, then the thread has no direct children to call wait on. */
  if(child_list == NULL || list_empty(child_list))
     return -1;

  /* Search for tid in the direct child list and store pointer in child_tid_node.
   * Pointer valid only if tid_found is true after loop. */
  bool tid_found = false; 
  struct list_elem *e;
  for (e = list_begin (child_list); e != list_end (child_list);
       e = list_next (e))
  {
      child_tid_node = list_entry (e, struct tid_status, elem);
      if(child_tid_node->tid == child_tid)
      {
         tid_found = true;
         break;
      }
  }

  /* Return if tid not in list, or if wait already called by parent on that child */
  if(!tid_found || child_tid_node->waiting == 1) 
    return -1;

  /* Perform main waiting. */
  int result = -2;
  sema_down(&child_tid_node->sema); // wait for child to die
  result = child_tid_node->exit_status;
  list_remove(&child_tid_node->elem);
  free(child_tid_node);

  /* Indicate that child has been waited for */
  child_tid_node->waiting = 1;

  return result;
}

/* Free the current process's resources. */
void
process_exit (void)
{  
  struct thread *cur = thread_current ();
  uint32_t *pd;
 
  /* close the executable file, if it exits */
  if(cur->executable != NULL)
    file_close(cur->executable); 

  /* set exit status, if not already set by call to exit */
  if(cur->tid_node->exit_status < 0)
  {
     cur->tid_node->exit_status = -1;
     printf("%s: exit(%d)\n", thread_name(), -1);
  }
  
  /* close all open files */
  struct fd_elem *cur_fd_node;
  while(!list_empty(&cur->fd_list))
  {
     cur_fd_node = list_entry(list_front(&cur->fd_list), struct fd_elem, elem);
     if(DBP)printf("closing fd %d\n", cur_fd_node->fd);

     lock_acquire(&file_lock);
     file_close(cur_fd_node->the_file);
     lock_release(&file_lock);

     /* clean up waste */
     list_remove(&cur_fd_node->elem);
     free(cur_fd_node->filename);
     free(cur_fd_node);
  }
  

 
  /* deallocate all remaining child tid_status elemnts, and signal to living children,
   * that the deallocation has occured */
  struct tid_status *cur_tid_node;
  while(!list_empty(&cur->child_nodes))
  {
     cur_tid_node = list_entry(list_front(&cur->child_nodes), struct tid_status, elem);
     if(DBP)printf("de-allocating tid_node for thread %d\n", cur_tid_node->tid);
     if(cur_tid_node->child != NULL)
        cur->tid_node->child->tid_node_exists = false;
     list_remove(&cur_tid_node->elem);
     free(cur_tid_node);

  }
  /* signal to non-waiting parent that the thread is about to die */
  if(cur->tid_node_exists)
     cur->tid_node->child = NULL;

  /* Signal to waiting parent that thread is about to die.*/
  sema_up(&cur->tid_node->sema);

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp, char **argv, int argc);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp, char **argv, int argc) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

  /* Open executable file. Deny write if successful */
  file = filesys_open (file_name);
  t->executable = file;
  if (file == NULL) 
  {
     printf ("load: %s: open failed\n", file_name);
     goto done; 
  }
  else
     file_deny_write(file);

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp, argv,argc))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;
  success = true;

  /* We arrive here whether the load is successful or not. */
done:
  return success;
}

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
   ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
   ASSERT (pg_ofs (upage) == 0);
   ASSERT (ofs % PGSIZE == 0);
 
   void * kpage = NULL;
   bool success;
   file_seek (file, ofs);
   while (read_bytes > 0 || zero_bytes > 0) 
   {

      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Get a page of memory. */
      kpage = palloc_page (PAL_USER, upage, writable);
      success = kpage; // true if kpage not NULL

      if(!success)
         return false; 

      /* Load this page. */
      if (kpage == NULL || file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
      {
         palloc_free_upage (upage);
         return false; 
      }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
   }
   return true;
}

/* allows the stack to grow if there isn't enough space
   for the pages to be allocated */
static uint8_t * grow_stack(uint8_t *prev)
{
   uint8_t *cur = prev - PGSIZE;
   if(palloc_page(PAL_USER | PAL_ZERO, cur, true))
      return cur;
   else
      return NULL;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp, char ** argv, int argc) 
{
  if(DBP)
  {
    printf("argc = %d\n", argc);
    int j;
    for(j = 0; j < argc; j++)
    {
      printf("argv[%d] = %s\n", j, argv[j]);
    }
  }
  char *arg_pointers[50];

  int i; // for various loops
  void * esp_prev; // used as a helper and for debuging

  // this code has been changed, no longer need access to a kpage
  uint8_t * upage = ((uint8_t *) PHYS_BASE) - PGSIZE;
  bool success = palloc_page (PAL_USER | PAL_ZERO, upage, true);
      if (success)
      {
        // initialize esp
        *esp = PHYS_BASE; 
        
        if(DBP) printf("esp initialized to %p\n\n", *esp);
 
        int arg_size;
	for(i = argc - 1; i >= 0; i--)
        {
	   //push argv[i] onto stack, and change esp
           arg_size = size_of_char_arg(argv[i]);
           esp_prev = *esp;
           *esp -= arg_size;
           strlcpy(*esp, argv[i], arg_size);           
	   arg_pointers[i] = *esp;

           if(DBP) 
           {
              printf("size of arg %d: %d\n", i, arg_size);
              printf("arg placed on stack: %s\n", (char *) *esp);
   	      printf("esp now: %p\n", *esp);
   	      printf("esp then: %p\n", esp_prev);
              printf("esp differenc: %d\n\n", esp_prev - *esp);
           }
	}
        // word align
        *esp = word_align(*esp, sizeof(void*));
    
        esp_prev = *esp;
        *esp -= sizeof(char *);
        memset(*esp, 0, sizeof(char*));

        if(DBP)
        {
           printf("completed mem copy\n");
           printf("esp now: %p\n", *esp);
   	   printf("esp then: %p\n", esp_prev);
           printf("esp differenc: %d\n\n", esp_prev - *esp);        
        }

  	// push pointers to rest of arguents
        void * esp_limit = PHYS_BASE - PGSIZE + 12; // to pervent overflow
  	for(i = argc - 1; i >= 0 && *esp >= esp_limit; i--)
 	{
 	    //push argv[i] onto stack, and change esp
           esp_prev = *esp;
           *esp -= sizeof(char *);

           memcpy(*esp, &arg_pointers[i], sizeof(char*));

           if(DBP)
           {
              printf("pointer placed on stack: %p\n", arg_pointers[i]);
              printf("arg pointed to: %s\n", (char *) arg_pointers[i]);
   	      printf("esp now: %p\n", *esp);
   	      printf("esp then: %p\n", esp_prev);
              printf("esp differenc: %d\n\n", esp_prev - *esp);
           }
 	}
        if(i >= 0) /* return in the event that args overflow stack */
        {
           palloc_free_upage (upage);
           success = false;
        }
	  
	// push pointer to pointer to above pointer to argv[0] 
        esp_prev = *esp;
        *esp -= sizeof(char *);         
        memcpy(*esp, &esp_prev, sizeof(char*)); 

   	if(DBP) printf("esp after argv pushed: %p\n", *esp);

 	// push argc 
        *esp -= sizeof(int);
        memcpy(*esp, &argc, sizeof(int));

        if(DBP) printf("esp after argc pushed: %p\n", *esp);

 	// push arbitrary return address
        *esp -= sizeof(char *);
        memset(*esp, 0, sizeof(void *)); // must change to memcpy if other val is used

        if(DBP) printf("esp after return address pushed: %p\n\n", *esp);

 	// print verification of stack
	if(DBP_DUMP) hex_dump((uintptr_t)*esp, *esp, PHYS_BASE - *esp, true);
     }
     else
        palloc_free_upage (upage);
  return success;
}

// compute the number of bytes in memory for a null-terminated string
inline int 
size_of_char_arg(char *arg)
{
   return (strlen(arg) + 1) * sizeof(char);
}

// returns a pointer to an address (lower or equal to add)
// that is a multiple of size 
inline void *
word_align(void* add, int size)
{
   return (void *)ROUND_DOWN((uintptr_t)add, size);
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails.
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  // Verify that there's not already a page at that virtual
  //   address, then map our page there. 
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}*/
