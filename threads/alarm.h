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
	int64_t sleep_time;     // how long will the thread sleep
	struct thread *th;      // pointer to the thread that's sleeping
  bool isOn;              // used to remove elapsed alarms
  struct list_elem elem;  // a "thing" used for lists..I have no idea
};

void alarm_handler_init(void);
void add_alarm(struct thread* th, int64_t sleep_time);
void update_alarms(int64_t ticks);

#endif /* ALARM_H_ */
