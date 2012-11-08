/*
 * alarm.c
 *
 *  Created on: Nov 4, 2012
 *      Author: osd
 */

#include "threads/alarm.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>

static struct list alarm_list;

void alarm_handler_init(void) {
	  list_init (&alarm_list);
}

void add_alarm(struct thread* th, int64_t sleep_time) {
	struct alarm buff;
	buff.th = th;
	buff.sleep_time = sleep_time;
	buff.isOn = true;

	//printf("adding alarm for thread %i, %ld, %ld\n",th->tid,sleep_time,start_time);
	int intr_level = intr_disable();
	list_push_back(&alarm_list, &buff.elem);
	thread_block();
	intr_set_level(intr_level);
}
void update_alarms(int64_t ticks){
  struct list_elem *e;
  if(!list_empty(&alarm_list)) 
  {
    for (e = list_begin (&alarm_list); e != list_end (&alarm_list);
         e = list_next (e))
    {
      struct alarm *alm = list_entry (e, struct alarm, elem);
      if(alm->isOn) 
      {
        if(alm->sleep_time <= 0 ) 
        {
          alm->isOn=false;
          //printf("unblocking thread %i at %i\n",alm->th->tid,alm->sleep_time);
          thread_unblock(alm->th);
        }
        else
          alm->sleep_time--;
      }
    }
  }

  // delete elapsed alarms
  e=list_begin(&alarm_list);
  while(e !=list_end(&alarm_list))
  {
    struct alarm *alm = list_entry (e, struct alarm, elem);
    if( !alm->isOn) {
      list_remove(e);
      e=list_begin(&alarm_list);
    } else {
    e = list_next(e);
    }
  }
}










