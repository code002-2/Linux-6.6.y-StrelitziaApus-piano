#ifndef _MCA_COMMON_MCA_WORKQUEUE_H_
#define _MCA_COMMON_MCA_WORKQUEUE_H_

#include <linux/workqueue.h>

int mca_queue_delayed_work(struct delayed_work *dwork, unsigned long delay);
int mca_mod_delayed_work(struct delayed_work *dwork, unsigned long delay);
int mca_queue_work(struct work_struct *work);
int mca_cancel_work(struct work_struct *work);
int mca_cancel_delayed_work(struct delayed_work *dwork);

#endif /* _MCA_COMMON_MCA_WORKQUEUE_H_ */
