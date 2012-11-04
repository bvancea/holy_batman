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

void add_alarm(struct thread* th, int64_t sleep_time, int64_t start_time) {
	struct alarm buff;
	buff.th = th;
	buff.start_time = start_time;
	buff.ticks_to_sleep = sleep_time;
	list_push_back(&alarm_list, &buff);
	intr_disable();
	thread_block();
	intr_enable();
	printf("added alarm");
}
void update_alarms(int64_t ticks){
	//printf("Time is %d\n", ticks);
}
