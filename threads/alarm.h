/*
 * alarm.h
 *
 *  Created on: Nov 4, 2012
 *      Author: logomorph
 */

#ifndef ALARM_H_
#define ALARM_H_

#include "threads/thread.h"

struct alarm {
	int64_t ticks_to_sleep;
	int64_t start_time;
	struct thread *th;
};

void alarm_handler_init(void);
void add_alarm(struct thread* th, int64_t sleep_time, int64_t start_time);
void update_alarms(int64_t ticks);

#endif /* ALARM_H_ */
