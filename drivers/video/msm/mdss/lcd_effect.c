#include "lcd_effect.h"

#define TAG "[LCD_EFFECT: ]"
//#define LCDDEBUG
#ifdef LCDDEBUG
#define lcd_effect_info(fmt, ...) printk(TAG fmt, ##__VA_ARGS__);
#else
#define lcd_effect_info(fmt, ...) do {} while (0)
#endif

static void update_effect_cmds(struct lcd_effect *effect, int level)
{
	struct lcd_effect_cmd_data *effect_cmd_data = &effect->effect_cmd_data;
	struct lcd_effect_cmds *effect_cmd = effect_cmd_data->effect_cmd;
	int cmd_cnt = effect_cmd_data->cnt;
	int code_cnt ;
	int i;

	for (i = 0; i < cmd_cnt; i++) {
		code_cnt = effect_cmd[i].effect_code.cnt;
		effect_cmd[i].lcd_cmd.cmd->payload = effect_cmd[i].effect_code.code[level >= code_cnt ? code_cnt -1 : level];
	}
}

static void inline update_level(struct lcd_effect *effect, int level)
{
	effect->level = level;
}

static inline void update_mode(struct lcd_mode_data *mode_data, int index)
{
	mode_data->current_mode = index;
}

static inline int get_level(struct lcd_effect *effect)
{
	return effect->level;
}

static inline int get_effect_cmd_cnt(struct lcd_effect *effect)
{
	return effect->effect_cmd_data.cnt;
}

static inline int get_head_cmd_cnt(struct lcd_cmds *head_cmd)
{
	return head_cmd->cnt;
}

static inline struct dsi_cmd_desc *get_head_cmd(struct lcd_cmds *head_cmd)
{
	return head_cmd->cmd;
}
static inline struct lcd_cmds *get_lcd_cmd(struct lcd_effect_cmds *effect_cmd)
{
	return &effect_cmd->lcd_cmd;
}
static inline struct lcd_effect_cmds * get_effect_cmd(struct lcd_effect *effect)
{
	return effect->effect_cmd_data.effect_cmd;
}

static inline struct dsi_cmd_desc *get_effect_cmd_desc(struct lcd_effect_cmds *effect_cmd)
{
	return effect_cmd->lcd_cmd.cmd;
}
static inline struct dsi_cmd_desc * get_mode_cmd(struct lcd_mode *mode)
{
	return mode->mode_cmd.cmd;
}
static inline int get_mode_cmd_cnt(struct lcd_mode *mode)
{
	return mode->mode_cmd.cnt;
}

static int get_mode_max_cnt(struct lcd_mode_data *mode_data)
{
	int i;
	int temp;
	int cnt = 0;

	for (i = 0; i < mode_data->supported_mode; i++) {
		temp = mode_data->mode[i].mode_cmd.cnt;
		cnt = (cnt > temp) ? cnt : temp;
		lcd_effect_info("%s cnt = %d temp = %d\n", __func__, cnt, temp);
	}

	return cnt;
}

static int get_effect_max_cnt(struct lcd_effect_data *effect_data)
{
	int cnt = 0;
	int temp;
	int i;

	for (i = 0; i < effect_data->supported_effect; i++) {
		temp = effect_data->effect[i].effect_cmd_data.cnt;
		cnt = cnt + temp;
		lcd_effect_info("%s cnt = %d temp = %d\n", __func__, cnt, temp);
	}

	return cnt;
}

static int get_init_code_max_cnt(struct panel_effect_data *panel_data, struct lcd_cmds *save_cmd)
{
	int cnt = save_cmd->cnt;

	cnt += get_mode_max_cnt(panel_data->mode_data);
	cnt += get_effect_max_cnt(panel_data->effect_data);
	lcd_effect_info("%s cnt: %d\n", __func__, cnt);
	return cnt;
}

static int send_lcd_cmds(struct msm_fb_data_type *mfd, struct lcd_cmds *cmds)
{
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	struct dcs_cmd_req cmdreq;
	struct mdss_panel_data *pdata;
	struct mdss_panel_info *pinfo;

	int ret;

	pdata = dev_get_platdata(&mfd->pdev->dev);
	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata, panel_data);
	if (mfd->panel_power_state == false) {
		pr_err("%s: LCD panel have powered off\n", __func__);
		return -EPERM;
	}
	
	pinfo = &(ctrl_pdata->panel_data.panel_info);
	if (pinfo->dcs_cmd_by_left) {
		if (ctrl_pdata->ndx != DSI_CTRL_LEFT)
			return 1;
	}

	memset(&cmdreq, 0, sizeof(cmdreq));
	cmdreq.flags = CMD_REQ_COMMIT | CMD_CLK_CTRL | CMD_REQ_LP_MODE;
	cmdreq.rlen = 0;
	cmdreq.cb = NULL;
	cmdreq.cmds = cmds->cmd;
	cmdreq.cmds_cnt = cmds->cnt;

	ret = mdss_dsi_cmdlist_put(ctrl_pdata, &cmdreq);

	return ret;
}

static struct dsi_cmd_desc *copy_init_code(struct panel_effect_data *panel_data, int *cnt)
{
	int init_cnt = panel_data->save_cmd.cnt;

	memcpy(panel_data->buf, panel_data->save_cmd.cmd, (init_cnt - CMDS_LAST_CNT) * sizeof (struct dsi_cmd_desc));
	*cnt += (init_cnt - CMDS_LAST_CNT);
	lcd_effect_info("%s: line=%d\n", __func__,__LINE__);
	return (panel_data->buf + (init_cnt - CMDS_LAST_CNT));
}

static struct dsi_cmd_desc *copy_sleep_out_code(
		struct panel_effect_data *panel_data, 
		struct dsi_cmd_desc *buf, 
		int *cnt)
{
	memcpy(buf, panel_data->save_cmd.cmd + panel_data->save_cmd.cnt - CMDS_LAST_CNT, CMDS_LAST_CNT * sizeof (struct dsi_cmd_desc));
	*cnt += CMDS_LAST_CNT;
	lcd_effect_info("%s: line=%d\n", __func__,__LINE__);
	return (buf + CMDS_LAST_CNT);
}
static struct dsi_cmd_desc *copy_head_code(struct panel_effect_data *panel_data, struct dsi_cmd_desc *buf, int *cnt)
{
	struct lcd_cmds *head_cmds = panel_data->effect_data->head_cmd;
	int head_cnt = get_head_cmd_cnt(head_cmds); 

	memcpy(buf, head_cmds->cmd, head_cnt * sizeof (struct dsi_cmd_desc));
	*cnt +=  head_cnt;

	lcd_effect_info("%s: line=%d\n", __func__,__LINE__);
	return (buf + head_cnt);
}

static struct dsi_cmd_desc * copy_single_effect_code(
		struct panel_effect_data *panel_data, 
		struct dsi_cmd_desc *buf, 
		int index, 
		int level,
		int *cnt)
{
	struct lcd_effect_data *effect_data = panel_data->effect_data;
	struct lcd_effect *effect = &effect_data->effect[index];
	struct dsi_cmd_desc *temp = buf;
	struct lcd_effect_cmds *effect_cmd;
	int cmd_cnt;
	int i;

	update_effect_cmds(effect, level);
	cmd_cnt = get_effect_cmd_cnt(effect);
	effect_cmd = get_effect_cmd(effect);
	*cnt += cmd_cnt;
	for (i = 0; i < cmd_cnt; i++)
		memcpy(temp++, get_effect_cmd_desc(&effect_cmd[i]), sizeof (struct dsi_cmd_desc));

	return (buf + cmd_cnt);
}

static struct dsi_cmd_desc *copy_used_effect_code(struct panel_effect_data *panel_data, struct dsi_cmd_desc *buf, int *cnt)
{
	struct dsi_cmd_desc *temp;
	struct lcd_effect_data *effect_data = panel_data->effect_data;
	struct lcd_effect *effect = effect_data->effect;

	temp = buf;
	//protect eys mode(ct level 3) is highest priority
	if((effect[EFFECT_CE].level) && (effect[EFFECT_CT].level!=3))
		temp = copy_single_effect_code(panel_data, temp, EFFECT_CE, effect[EFFECT_CE].level, cnt);
	else
		temp = copy_single_effect_code(panel_data, temp, EFFECT_CT, effect[EFFECT_CT].level, cnt);

	temp = copy_single_effect_code(panel_data, temp, EFFECT_CABC, effect[EFFECT_CABC].level, cnt);
	temp = copy_single_effect_code(panel_data, temp, EFFECT_HBM, effect[EFFECT_HBM].level, cnt);

	lcd_effect_info("%s,EFFECT_CE level:%d,EFFECT_CT level:%d,EFFECT_CABC level:%d,EFFECT_HBM level:%d\n",__func__,
	effect[EFFECT_CE].level,effect[EFFECT_CT].level,effect[EFFECT_CABC].level,effect[EFFECT_HBM].level);

	return temp;
}

static struct dsi_cmd_desc *copy_all_effect_code(struct panel_effect_data *panel_data, struct dsi_cmd_desc *buf, int *cnt)
{
	struct dsi_cmd_desc *temp;
	struct lcd_effect_data *effect_data = panel_data->effect_data;
	struct lcd_effect *effect = effect_data->effect;
	struct lcd_effect_cmds *effect_cmd;
	int i, j;
	int cmd_cnt;

	temp = buf;
	for (i = 0; i < effect_data->supported_effect; i++) {
		update_effect_cmds(&effect[i], effect[i].level);
		cmd_cnt = get_effect_cmd_cnt(&effect[i]);
		effect_cmd = get_effect_cmd(&effect[i]);
		lcd_effect_info("%s name: [%s] level: [%d],cmd_cnt:[%d]\n", __func__,
			effect[i].name, effect[i].level,cmd_cnt);
		*cnt += cmd_cnt;
		for (j = 0; j < cmd_cnt; j++)
			memcpy(temp++, get_effect_cmd_desc(&effect_cmd[j]), sizeof (struct dsi_cmd_desc));
	}

	return temp;
}

static struct dsi_cmd_desc * copy_mode_code(
		struct panel_effect_data *panel_data, 
		struct dsi_cmd_desc *buf, 
		int mode_index, 
		int *cnt)
{
	struct lcd_mode *mode = &panel_data->mode_data->mode[mode_index];
	struct lcd_cmds *mode_cmds = &mode->mode_cmd; 
	struct dsi_cmd_desc *temp;
	int count = 0;
	int mode_cnt = get_mode_cmd_cnt(mode);
	lcd_effect_info("%s: line=%d mode_cnt=%d\n", __func__,__LINE__,mode_cnt);
	if (mode_index == 0) {
		lcd_effect_info("%s: current is custom mode\n", __func__);
		temp = copy_all_effect_code(panel_data, buf, &count);
		*cnt += count;
	} else {
		lcd_effect_info("%s: current is %s\n", __func__, mode->name);
		memcpy(buf, mode_cmds->cmd, mode_cnt * sizeof (struct dsi_cmd_desc));
		temp = buf + mode_cnt;
		*cnt += mode_cnt;
	}

	return temp;
}

static int set_mode(struct msm_fb_data_type *mfd, struct panel_effect_data *panel_data, int index)
{
	struct lcd_cmds lcd_cmd;
	struct dsi_cmd_desc *temp;
	int cnt = 0;
	int ret;

	lcd_cmd.cmd = panel_data->buf;

	temp = copy_head_code(panel_data, panel_data->buf, &cnt);
	copy_mode_code(panel_data, temp, index, &cnt);

	lcd_cmd.cnt = cnt;

	ret = send_lcd_cmds(mfd, &lcd_cmd);
	if (ret >= 0 || ret == -EPERM) {
		panel_data->mode_data->current_mode = index;
		lcd_effect_info("%s %s success\n", __func__, panel_data->mode_data->mode[index].name);
		ret = 0;
	}

	return ret;
}

static int set_effect(struct msm_fb_data_type *mfd, struct panel_effect_data *panel_data, int index, int level)
{
	struct lcd_cmds lcd_cmd;
	struct dsi_cmd_desc *temp;
	int cnt = 0;
	int ret;

	lcd_cmd.cmd = panel_data->buf;

	temp = copy_head_code(panel_data, panel_data->buf, &cnt);
	copy_single_effect_code(panel_data, temp, index, level, &cnt);

	lcd_cmd.cnt = cnt;

	ret = send_lcd_cmds(mfd, &lcd_cmd);

	if (ret >= 0 || ret == -EPERM) {
		panel_data->effect_data->effect[index].level = level;
		lcd_effect_info("%s name: [%s] level: [%d] success\n", __func__, panel_data->effect_data->effect[index].name, level);
		ret = 0;
	}

	return ret;
}

int update_init_code(
		struct mdss_dsi_ctrl_pdata *ctrl_pdata,
		struct panel_effect_data *panel_data,
		void (*mdss_dsi_panel_cmds_send)(struct mdss_dsi_ctrl_pdata *ctrl,struct dsi_panel_cmds *pcmds,u32 flags))
{
	struct lcd_cmds lcd_cmd;
	struct dsi_cmd_desc *temp;
	struct lcd_cmds *save_cmd = &panel_data->save_cmd;
	int cnt = 0;
	int ret = 0;
	lcd_cmd.cmd = panel_data->buf;

	temp = copy_init_code(panel_data, &cnt);

	temp = copy_used_effect_code(panel_data, temp, &cnt);
	temp = copy_sleep_out_code(panel_data, temp, &cnt);

	lcd_cmd.cnt = cnt;

	ctrl_pdata->on_cmds.cmds = lcd_cmd.cmd;
	ctrl_pdata->on_cmds.cmd_cnt = lcd_cmd.cnt;
	lcd_effect_info("%s Use system param\n", __func__);

	mdss_dsi_panel_cmds_send(ctrl_pdata, &ctrl_pdata->on_cmds,CMD_REQ_COMMIT);


	ctrl_pdata->on_cmds.cmds = save_cmd->cmd;
	ctrl_pdata->on_cmds.cmd_cnt = save_cmd->cnt;
	return ret;
}


int malloc_lcd_effect_code_buf(struct panel_effect_data *panel_data)
{
	struct lcd_cmds *save_cmd = &panel_data->save_cmd;
	if (panel_data->buf == NULL) {
		panel_data->buf_size = get_init_code_max_cnt(panel_data, save_cmd);
		panel_data->buf  = kmalloc(sizeof(struct dsi_cmd_desc) * panel_data->buf_size, GFP_KERNEL);
		if ( !panel_data->buf)
			return -ENOMEM;
		return 0;
	}
	return 0;
}
