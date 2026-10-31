#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/completion.h>
#include <linux/workqueue.h>
#include <linux/list.h>
#include <linux/string.h>
#include <linux/err.h>
#include <linux/of_device.h>
#include <linux/soc/qcom/qti_pmic_glink.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_adsp_glink.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_adsp_glink"
#endif

#define MCA_ADSP_GLINK_OWNER 0x800A
#define MCA_ADSP_GLINK_QBG_OWNER 0x8009
#define MCA_ADSP_GLINK_MSG_REQ 1 /* hdr.type: request/response */
#define MCA_ADSP_GLINK_MSG_NOTIFY 2 /* hdr.type: async notification */
#define MCA_ADSP_GLINK_OPCODE_READ 1
#define MCA_ADSP_GLINK_OPCODE_WRITE 2
#define MCA_ADSP_PROP_DATA_LEN 256
#define MCA_ADSP_GLINK_TIMEOUT_MS 1000

struct mca_adsp_glink_req_msg {
	struct pmic_glink_hdr hdr;
	u32 property_id;
	u32 seq;
	u8 data[MCA_ADSP_PROP_DATA_LEN];
} __packed;

struct mca_adsp_glink_resp_msg {
	struct pmic_glink_hdr hdr;
	u32 property_id;
	u32 retcode;
	u32 seq;
	u8 data[MCA_ADSP_PROP_DATA_LEN];
} __packed;

struct mca_adsp_glink_notify_msg {
	struct pmic_glink_hdr hdr;
	u32 property_id;
	u8 data[MCA_ADSP_PROP_DATA_LEN];
} __packed;

struct mca_adsp_glink_dev {
	struct device *dev;
	struct pmic_glink_client *client;
	struct pmic_glink_client *qbg_client;
	struct mutex rw_lock;
	struct completion ack;
	int glink_state;
	struct work_struct sync_work;
	u32 cur_property_id;
	int retcode;
	u32 seq;
	u8 read_buf[MCA_ADSP_PROP_DATA_LEN];
};

struct mca_adsp_ops_node {
	struct list_head node;
	void *priv;
	struct mca_adsp_glink_ops *ops;
};

static struct mca_adsp_glink_dev *g_mca_adsp_glink;
static LIST_HEAD(g_mca_adsp_glink_ops_head);
static LIST_HEAD(g_mca_adsp_glink_qbg_ops_head);

int mca_adsp_glink_write_prop(int prop_id, void *value, size_t size)
{
	struct mca_adsp_glink_dev *mca = g_mca_adsp_glink;
	struct mca_adsp_glink_req_msg msg = { 0 };
	int ret = -1;

	if (!value || size >= MCA_ADSP_PROP_DATA_LEN || !mca)
		return -1;

	mutex_lock(&mca->rw_lock);
	mca->seq++;
	msg.hdr.owner = MCA_ADSP_GLINK_OWNER;
	msg.hdr.type = MCA_ADSP_GLINK_MSG_REQ;
	msg.hdr.opcode = MCA_ADSP_GLINK_OPCODE_WRITE;
	msg.property_id = prop_id;
	msg.seq = mca->seq;
	memcpy(msg.data, value, size);

	if (mca->glink_state == 0) {
		mca_log_err("glink state is down\n");
		ret = -ENOTCONN;
		goto out;
	}

	reinit_completion(&mca->ack);
	mca->retcode = 0;
	mca->cur_property_id = prop_id;
	ret = pmic_glink_write(mca->client, &msg, sizeof(msg));
	if (ret == 0) {
		if (wait_for_completion_timeout(
			    &mca->ack,
			    msecs_to_jiffies(MCA_ADSP_GLINK_TIMEOUT_MS)) == 0) {
			mca_log_err("timed out sending message, prop_id: %d\n",
				    mca->cur_property_id);
			ret = -ETIMEDOUT;
		} else {
			ret = mca->retcode;
		}
		mca->cur_property_id = 0xffffffff;
	}
out:
	mutex_unlock(&mca->rw_lock);
	return ret;
}
EXPORT_SYMBOL(mca_adsp_glink_write_prop);

int mca_adsp_glink_read_prop(int prop_id, void *value, size_t size)
{
	struct mca_adsp_glink_dev *mca = g_mca_adsp_glink;
	struct mca_adsp_glink_req_msg msg = { 0 };
	int ret = -1;

	if (!value || size >= MCA_ADSP_PROP_DATA_LEN || !mca)
		return -1;

	mutex_lock(&mca->rw_lock);
	mca->seq++;
	msg.hdr.owner = MCA_ADSP_GLINK_OWNER;
	msg.hdr.type = MCA_ADSP_GLINK_MSG_REQ;
	msg.hdr.opcode = MCA_ADSP_GLINK_OPCODE_READ;
	msg.property_id = prop_id;
	msg.seq = mca->seq;

	if (mca->glink_state == 0) {
		mca_log_err("glink state is down\n");
		ret = -ENOTCONN;
		goto out;
	}

	reinit_completion(&mca->ack);
	mca->retcode = 0;
	mca->cur_property_id = prop_id;
	ret = pmic_glink_write(mca->client, &msg, sizeof(msg));
	if (ret == 0) {
		if (wait_for_completion_timeout(
			    &mca->ack,
			    msecs_to_jiffies(MCA_ADSP_GLINK_TIMEOUT_MS)) == 0) {
			mca_log_err("timed out sending message, prop_id: %d\n",
				    mca->cur_property_id);
			ret = -ETIMEDOUT;
		} else {
			ret = mca->retcode;
			if (ret == 0) {
				memcpy(value, mca->read_buf, size);
				memset(mca->read_buf, 0,
				       MCA_ADSP_PROP_DATA_LEN);
			}
		}
		mca->cur_property_id = 0xffffffff;
	}
out:
	mutex_unlock(&mca->rw_lock);
	return ret;
}
EXPORT_SYMBOL(mca_adsp_glink_read_prop);

int mca_adsp_glink_resister_ops(struct mca_adsp_glink_ops *ops, void *priv)
{
	struct mca_adsp_ops_node *n;

	n = kzalloc(sizeof(*n), GFP_KERNEL);
	if (!n)
		return -ENOMEM;

	n->ops = ops;
	n->priv = priv;
	list_add(&n->node, &g_mca_adsp_glink_ops_head);
	return 0;
}
EXPORT_SYMBOL(mca_adsp_glink_resister_ops);

int mca_adsp_glink_qbg_write_prop(int prop_id, void *value, size_t size)
{
	struct mca_adsp_glink_dev *mca = g_mca_adsp_glink;
	struct mca_adsp_glink_req_msg msg = { 0 };
	int ret = -1;

	if (!value || size >= MCA_ADSP_PROP_DATA_LEN || !mca)
		return -1;

	mutex_lock(&mca->rw_lock);
	mca->seq++;
	msg.hdr.owner = MCA_ADSP_GLINK_QBG_OWNER;
	msg.hdr.type = MCA_ADSP_GLINK_MSG_REQ;
	msg.hdr.opcode = MCA_ADSP_GLINK_OPCODE_WRITE;
	msg.property_id = prop_id;
	msg.seq = mca->seq;
	memcpy(msg.data, value, size);

	if (mca->glink_state == 0) {
		mca_log_err("glink state is down\n");
		ret = -ENOTCONN;
		goto out;
	}

	reinit_completion(&mca->ack);
	mca->retcode = 0;
	mca->cur_property_id = prop_id;
	ret = pmic_glink_write(mca->client, &msg, sizeof(msg));
	if (ret == 0) {
		if (wait_for_completion_timeout(
			    &mca->ack,
			    msecs_to_jiffies(MCA_ADSP_GLINK_TIMEOUT_MS)) == 0) {
			mca_log_err("timed out sending message, prop_id: %d\n",
				    mca->cur_property_id);
			ret = -ETIMEDOUT;
		} else {
			ret = mca->retcode;
		}
		mca->cur_property_id = 0xffffffff;
	}
out:
	mutex_unlock(&mca->rw_lock);
	return ret;
}
EXPORT_SYMBOL(mca_adsp_glink_qbg_write_prop);

int mca_adsp_glink_qbg_read_prop(int prop_id, void *value, size_t size)
{
	struct mca_adsp_glink_dev *mca = g_mca_adsp_glink;
	struct mca_adsp_glink_req_msg msg = { 0 };
	int ret = -1;

	if (!value || size >= MCA_ADSP_PROP_DATA_LEN || !mca)
		return -1;

	mutex_lock(&mca->rw_lock);
	mca->seq++;
	msg.hdr.owner = MCA_ADSP_GLINK_QBG_OWNER;
	msg.hdr.type = MCA_ADSP_GLINK_MSG_REQ;
	msg.hdr.opcode = MCA_ADSP_GLINK_OPCODE_READ;
	msg.property_id = prop_id;
	msg.seq = mca->seq;

	if (mca->glink_state == 0) {
		mca_log_err("glink state is down\n");
		ret = -ENOTCONN;
		goto out;
	}

	reinit_completion(&mca->ack);
	mca->retcode = 0;
	mca->cur_property_id = prop_id;
	ret = pmic_glink_write(mca->client, &msg, sizeof(msg));
	if (ret == 0) {
		if (wait_for_completion_timeout(
			    &mca->ack,
			    msecs_to_jiffies(MCA_ADSP_GLINK_TIMEOUT_MS)) == 0) {
			mca_log_err("timed out sending message, prop_id: %d\n",
				    mca->cur_property_id);
			ret = -ETIMEDOUT;
		} else {
			ret = mca->retcode;
			if (ret == 0) {
				memcpy(value, mca->read_buf, size);
				memset(mca->read_buf, 0,
				       MCA_ADSP_PROP_DATA_LEN);
			}
		}
		mca->cur_property_id = 0xffffffff;
	}
out:
	mutex_unlock(&mca->rw_lock);
	return ret;
}
EXPORT_SYMBOL(mca_adsp_glink_qbg_read_prop);

int mca_adsp_glink_qbg_resister_ops(struct mca_adsp_glink_ops *ops, void *priv)
{
	struct mca_adsp_ops_node *n;

	n = kzalloc(sizeof(*n), GFP_KERNEL);
	if (!n)
		return -ENOMEM;

	n->ops = ops;
	n->priv = priv;
	list_add(&n->node, &g_mca_adsp_glink_qbg_ops_head);
	return 0;
}
EXPORT_SYMBOL(mca_adsp_glink_qbg_resister_ops);

static int
mca_adsp_glink_handle_notification(struct mca_adsp_glink_dev *mca,
				   struct mca_adsp_glink_notify_msg *msg)
{
	struct mca_adsp_ops_node *n;
	struct list_head *head;

	head = (msg->hdr.owner == MCA_ADSP_GLINK_QBG_OWNER) ?
		       &g_mca_adsp_glink_qbg_ops_head :
		       &g_mca_adsp_glink_ops_head;

	mca_log_info("mca_adsp receive notification %d, owner %d\n",
		     msg->property_id, msg->hdr.owner);
	list_for_each_entry(n, head, node) {
		if (n->ops && n->ops->notification)
			n->ops->notification(msg->property_id, msg->data,
					     MCA_ADSP_PROP_DATA_LEN, n->priv);
	}
	return 0;
}

static int mca_adsp_glink_handle_message(struct mca_adsp_glink_dev *mca,
					 struct mca_adsp_glink_resp_msg *msg,
					 size_t len)
{
	if (len == sizeof(*msg) && msg->retcode == 0) {
		if (msg->seq != mca->seq) {
			mca_log_err("invalid seq_num: %d != %d\n", mca->seq,
				    msg->seq);
			return 0;
		}
		if (msg->hdr.opcode == MCA_ADSP_GLINK_OPCODE_READ)
			memcpy(mca->read_buf, msg->data,
			       MCA_ADSP_PROP_DATA_LEN);
	} else {
		mca_log_err(
			"glink retcode error, property_id: %d, retcode: %d, len: %lu %lu\n",
			msg->property_id, msg->retcode, len, sizeof(*msg));
		mca->retcode = msg->retcode;
	}
	complete(&mca->ack);
	return 0;
}

static int mca_adsp_glink_callback(void *priv, void *data, size_t len)
{
	struct mca_adsp_glink_dev *mca = priv;
	struct pmic_glink_hdr *hdr = data;

	if (hdr->type == MCA_ADSP_GLINK_MSG_NOTIFY)
		return mca_adsp_glink_handle_notification(mca, data);

	return mca_adsp_glink_handle_message(mca, data, len);
}

static void mca_adsp_glink_sync_work(struct work_struct *work)
{
	struct mca_adsp_ops_node *n;

	list_for_each_entry(n, &g_mca_adsp_glink_ops_head, node) {
		if (n->ops && n->ops->glink_state_up)
			n->ops->glink_state_up(n->priv);
	}
	list_for_each_entry(n, &g_mca_adsp_glink_qbg_ops_head, node) {
		if (n->ops && n->ops->glink_state_up)
			n->ops->glink_state_up(n->priv);
	}
}

static void mca_adsp_glink_state_cb(void *priv, enum pmic_glink_state state)
{
	struct mca_adsp_glink_dev *mca = priv;
	struct mca_adsp_ops_node *n;

	mca->glink_state = state;
	if (state == PMIC_GLINK_STATE_UP) {
		queue_work(system_wq, &mca->sync_work);
	} else {
		list_for_each_entry(n, &g_mca_adsp_glink_ops_head, node) {
			if (n->ops && n->ops->glink_state_down)
				n->ops->glink_state_down(n->priv);
		}
		list_for_each_entry(n, &g_mca_adsp_glink_qbg_ops_head, node) {
			if (n->ops && n->ops->glink_state_down)
				n->ops->glink_state_down(n->priv);
		}
	}
}

static int mca_adsp_glink_probe(struct platform_device *pdev)
{
	struct mca_adsp_glink_dev *mca;
	struct pmic_glink_client_data client_data = { 0 };
	int ret;

	mca = devm_kzalloc(&pdev->dev, sizeof(*mca), GFP_KERNEL);
	if (!mca)
		return -ENOMEM;

	mca->dev = &pdev->dev;
	mca->cur_property_id = 0xffffffff;
	mutex_init(&mca->rw_lock);
	init_completion(&mca->ack);
	mca->glink_state = 1;
	INIT_WORK(&mca->sync_work, mca_adsp_glink_sync_work);

	client_data.name = "mca_adap_glink";
	client_data.id = MCA_ADSP_GLINK_OWNER;
	client_data.priv = mca;
	client_data.msg_cb = mca_adsp_glink_callback;
	client_data.state_cb = mca_adsp_glink_state_cb;

	mca->client = pmic_glink_register_client(&pdev->dev, &client_data);
	if (IS_ERR(mca->client)) {
		ret = PTR_ERR(mca->client);
		if (ret == -EPROBE_DEFER)
			return ret;
		mca_log_err("Error in registering with pmic_glink %d\n", ret);
		return ret;
	}

	/*
	 * pmic_glink hands each message to the client whose id matches its
	 * owner, so the QBG replies need a client of their own even though
	 * the requests go out on the one above.
	 */
	client_data.name = "mca_adap_qbg_glink";
	client_data.id = MCA_ADSP_GLINK_QBG_OWNER;

	mca->qbg_client = pmic_glink_register_client(&pdev->dev, &client_data);
	if (IS_ERR(mca->qbg_client)) {
		ret = PTR_ERR(mca->qbg_client);
		pmic_glink_unregister_client(mca->client);
		if (ret == -EPROBE_DEFER)
			return ret;
		mca_log_err("Error in registering with qbg_glink %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, mca);
	g_mca_adsp_glink = mca;
	mca_log_err("probe ok\n");
	return 0;
}

static int mca_adsp_glink_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id mca_adsp_match_table[] = {
	{ .compatible = "mca,adsp_glink" },
	{},
};

static struct platform_driver mca_adsp_driver = {
	.driver = {
		.name = "mca_adsp_glink",
		.of_match_table = mca_adsp_match_table,
	},
	.probe = mca_adsp_glink_probe,
	.remove = mca_adsp_glink_remove,
};
module_platform_driver(mca_adsp_driver);

MODULE_DESCRIPTION("mca glink driver");
MODULE_AUTHOR("liyuze1@xiaomi.com");
MODULE_LICENSE("GPL v2");
