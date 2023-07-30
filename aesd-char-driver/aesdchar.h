/*
 * aesdchar.h
 *
 *  Created on: Oct 23, 2019
 *      Author: Dan Walkes
 */

#ifndef AESD_CHAR_DRIVER_AESDCHAR_H_
#define AESD_CHAR_DRIVER_AESDCHAR_H_

#define AESD_DEBUG 1  //Remove comment on this line to enable debug

#undef PDEBUG             /* undef it, just in case */
#ifdef AESD_DEBUG
#  ifdef __KERNEL__
     /* This one if debugging is on, and kernel space */
#    define PDEBUG(fmt, args...) printk( KERN_DEBUG "aesdchar: " fmt, ## args)
#  else
     /* This one for user space */
#    define PDEBUG(fmt, args...) fprintf(stderr, fmt, ## args)
#  endif
#else
#  define PDEBUG(fmt, args...) /* not debugging: nothing */
#endif

/* Queue for for storing command*/
#include "aesd-circular-buffer.h"
#include "queue.h"


typedef struct qentry_node{
    aesd_buffer_entry_t *entry;
    STAILQ_ENTRY(qentry_node) next;
} qentry_node_t;

typedef STAILQ_HEAD(aesd_buffer_entry_queue, qentry_node) queue_t;

struct aesd_dev{
    /* Buffer section*/
    aesd_circular_buffer_t circ_buf;
    unsigned long circ_buf_size;       /* amount of data stored here */
    struct mutex circ_buf_lock;

    queue_t queue;
    size_t queue_size;		/* amount of data stored in queue */
    struct mutex queue_lock;


    struct cdev cdev;     /* Char device structure      */
};


#endif /* AESD_CHAR_DRIVER_AESDCHAR_H_ */