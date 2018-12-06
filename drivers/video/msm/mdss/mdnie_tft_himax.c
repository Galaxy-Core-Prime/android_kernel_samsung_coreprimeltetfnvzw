/* Copyright (c) 2009-2011, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/fs.h>
#include <linux/irq.h>
#include <linux/mm.h>
#include <linux/fb.h>
#include <linux/ctype.h>
#include <linux/device.h>

#include "mdss_fb.h"
#include "mdss_panel.h"
#include "mdss_dsi.h"
#include "mdnie_tft_msm8x16.h"

#if defined(CONFIG_FB_MSM_MIPI_HIMAX_WVGA_VIDEO_PANEL)
#include "mdnie_tft_data_wvga_hx8369b.h"
#endif


#define MDNIE_TFT_DEBUG

#ifdef MDNIE_TFT_DEBUG
#define DPRINT(x...)	printk(KERN_ERR "[mdnie lite] " x)
#else
#define DPRINT(x...)
#endif

#define MAX_LUT_SIZE	256


static char NEGATIVE_MODE_ON_CMDS[] = {0x21, 0x00};
static char NEGATIVE_MODE_OFF_CMDS[] = {0x20, 0x00};
static int is_ldi_himax = 0;
struct mutex mdnie_lock;

int play_speed_1_5;

struct dsi_buf dsi_mdnie_tx_buf;

static struct mdss_samsung_driver_data *mdnie_msd;

struct mdnie_tft_type mdnie_tun_state = {
	.mdnie_enable = false,
	.scenario = mDNIe_UI_MODE,
	.background = STANDARD_MODE,
	.outdoor = OUTDOOR_OFF_MODE,
	.negative = mDNIe_NEGATIVE_OFF,
	.blind = ACCESSIBILITY_OFF,
};

const char background_name[MAX_BACKGROUND_MODE][16] = {
	"DYNAMIC",
	"STANDARD",
	"MOVIE",
	"NATURAL",
};

const char scenario_name[MAX_mDNIe_MODE][16] = {
	"UI_MODE",
	"VIDEO_MODE",
	"VIDEO_WARM_MODE",
	"VIDEO_COLD_MODE",
	"CAMERA_MODE",
	"NAVI",
	"GALLERY_MODE",
	"VT_MODE",
	"BROWSER",
	"eBOOK",
#if defined(CONFIG_TDMB)
	"DMB_MODE",
	"DMB_WARM_MODE",
	"DMB_COLD_MODE",
#endif
};


static struct dsi_cmd_desc mdni_tune_cmd[1]= {
		{{DTYPE_DCS_LWRITE, 1, 0, 0, 0, 0}, NULL},
};

#define SET_MDNIE_COMMAND(x) \
		mutex_lock(&mdnie_lock);\
		mdni_tune_cmd[0].dchdr.dlen =sizeof(x);	\
		mdni_tune_cmd[0].payload =x;

#define CLEAR_MDNIE_COMMAND() \
		mdni_tune_cmd[0].dchdr.dlen =0;	\
		mdni_tune_cmd[0].payload =NULL;	\
		mutex_unlock(&mdnie_lock);


void sending_tuning_cmd(void)
{
	struct msm_fb_data_type *mfd;
	struct mdss_panel_data *pdata;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata;

	mfd = mdnie_msd->mfd;
	pdata = mdnie_msd->mpd;
	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
						panel_data);

	mutex_lock(&mdnie_msd->lock);

	if (mfd->resume_state == MIPI_SUSPEND_STATE) {
		mutex_unlock(&mdnie_msd->lock);
		DPRINT(" power off!!\n");
	} else {
		DPRINT(" send tuning cmd!! => %d - 0x%x\n", mdni_tune_cmd[0].dchdr.dlen, (unsigned int)mdni_tune_cmd[0].payload);
		mdss_dsi_cmds_send(ctrl_pdata, &mdni_tune_cmd[0], 1,0);
		mutex_unlock(&mdnie_msd->lock);
	}
}

void mDNIe_Set_Mode(enum Lcd_mDNIe_UI mode)
{
	struct msm_fb_data_type *mfd;
	mfd = mdnie_msd->mfd;

	DPRINT("mDNIe_Set_Mode start , mode(%d), background(%d)\n",
		mode, mdnie_tun_state.background);

	if (mfd->resume_state == MIPI_SUSPEND_STATE) {
		DPRINT("[ERROR] not ST_DSI_RESUME. do not send mipi cmd.\n");
		return;
	}
	if (!mdnie_tun_state.mdnie_enable) {
		DPRINT("[ERROR] mDNIE engine is OFF.\n");
		return;
	}

	if (mode < mDNIe_UI_MODE || mode >= MAX_mDNIe_MODE) {
		DPRINT("[ERROR] wrong Scenario mode value : %d\n",
			mode);
		return;
	}

	if (mdnie_tun_state.negative) {
		DPRINT("already negative mode(%d), do not set background(%d)\n",
			mdnie_tun_state.negative, mdnie_tun_state.background);
		return;
	}

	play_speed_1_5 = 0;

	/*
	*	Blind mode & Screen mode has separated menu.
	*	To make a sync below code added.
	*	Bline mode has priority than Screen mode
	*/
	if (mdnie_tun_state.blind == COLOR_BLIND)
		mode = mDNIE_BLINE_MODE;

	switch (mode) {
	case mDNIe_UI_MODE:
		DPRINT(" = UI MODE =\n");
		SET_MDNIE_COMMAND(mDNIe_UI_MODE_CMDS)
		break;

	case mDNIe_VIDEO_MODE:
		DPRINT(" = VIDEO MODE =\n");
		SET_MDNIE_COMMAND(mDNIe_VIDEO_MODE_CMDS)
		break;

	case mDNIe_VIDEO_WARM_MODE:
		DPRINT(" = VIDEO WARM MODE =\n");
		SET_MDNIE_COMMAND(mDNIe_VIDEO_WARM_MODE_CMDS)
		break;

	case mDNIe_VIDEO_COLD_MODE:
		DPRINT(" = VIDEO COLD MODE =\n");
		SET_MDNIE_COMMAND(mDNIe_VIDEO_COLD_MODE_CMDS)
		break;

	case mDNIe_CAMERA_MODE:
		DPRINT(" = CAMERA MODE =\n");
		SET_MDNIE_COMMAND(mDNIe_CAMERA_MODE_CMDS)
		break;

	case mDNIe_NAVI:
		DPRINT(" = NAVI MODE =\n");
		SET_MDNIE_COMMAND(mDNIe_NAVI_MODE_CMDS)
		break;

	case mDNIe_GALLERY:
		DPRINT(" = GALLERY MODE =\n");
		SET_MDNIE_COMMAND(mDNIe_GALLERY_MODE_CMDS)
		break;

	case mDNIe_VT_MODE:
		DPRINT(" = VT MODE =\n");
		SET_MDNIE_COMMAND(mDNIe_VT_MODE_CMDS)
		break;

#if defined(CONFIG_TDMB)
	case mDNIe_DMB_MODE:
		DPRINT(" = DMB MODE =\n");
		DPRINT("no data for DMB MODE..\n");
		return;


	case mDNIe_DMB_WARM_MODE:
		DPRINT(" = DMB WARM MODE =\n");
		DPRINT("no data for DMB  WARM MODE..\n");
		return;


	case mDNIe_DMB_COLD_MODE:
		DPRINT(" = DMB COLD MODE =\n");
		DPRINT("no data for DMB COLD MODE..\n");
		return;

#endif

#if defined(CONFIG_MTV)
	case mDNIe_ISDBT_SOCCER_MODE:
		DPRINT(" = ISDB SOCCER MODE =\n");
		SET_MDNIE_COMMAND(mDNIe_ISDBT_SOCCER_MODE_CMD)
		break;
#endif

	case mDNIe_BROWSER_MODE:
		DPRINT(" = BROWSER MODE =\n");
		DPRINT("no data for DMB MODE..\n");
		return;

	case mDNIe_eBOOK_MODE:
		DPRINT(" = eBOOK MODE =\n");
		DPRINT("no data for DMB MODE..\n");
		return;

	case mDNIe_EMAIL_MODE:
		DPRINT(" = EMAIL MODE =\n");
		DPRINT("no data for DMB MODE..\n");
		return;

	case mDNIE_BLINE_MODE:
		DPRINT(" = BLIND MODE =\n");
		DPRINT("no data for DMB MODE..\n");
		return;
	default:
		DPRINT("[%s] no option (%d)\n", __func__, mode);
		return;
	}

	sending_tuning_cmd();
	CLEAR_MDNIE_COMMAND()

	DPRINT("mDNIe_Set_Mode end , mode(%d), background(%d)\n",
		mode, mdnie_tun_state.background);

}
void is_negative_on(void)
{
	DPRINT("is negative Mode On = %d\n", mdnie_tun_state.negative);

	if (mdnie_tun_state.negative) {
		DPRINT("mDNIe_Set_Negative = %d\n", mdnie_tun_state.negative);
		DPRINT(" = NEGATIVE MODE =\n");

		if(!is_ldi_himax) {
			SET_MDNIE_COMMAND(NEGATIVE_MODE_ON_CMDS)
			sending_tuning_cmd();
			CLEAR_MDNIE_COMMAND()
		} else {
			SET_MDNIE_COMMAND(mDNIe_NEGATIVE_MODE_CMDS)
			sending_tuning_cmd();
			CLEAR_MDNIE_COMMAND()
		}
	} else {
		/* check the mode and tuning again when wake up*/
		DPRINT("negative off when resume, tuning again!\n");
		if(!is_ldi_himax) {
			SET_MDNIE_COMMAND(NEGATIVE_MODE_OFF_CMDS)
			sending_tuning_cmd();
			CLEAR_MDNIE_COMMAND()
		}
		mDNIe_Set_Mode(mdnie_tun_state.scenario);
	}
}

void mDNIe_set_negative(enum Lcd_mDNIe_Negative negative)
{
	DPRINT("mDNIe_Set_Negative state:%d\n",negative);
	if (negative == 0) {
		DPRINT("Negative mode(%d) -> reset mode(%d)\n",
			mdnie_tun_state.negative, mdnie_tun_state.scenario);
		if(!is_ldi_himax) {
			SET_MDNIE_COMMAND(NEGATIVE_MODE_OFF_CMDS)
			sending_tuning_cmd();
			CLEAR_MDNIE_COMMAND()
		}
		mDNIe_Set_Mode(mdnie_tun_state.scenario);
	} else {

		DPRINT("mDNIe_Set_Negative = %d\n", mdnie_tun_state.negative);
		DPRINT(" = NEGATIVE MODE =\n");
		if(!is_ldi_himax) {
			SET_MDNIE_COMMAND(NEGATIVE_MODE_ON_CMDS)
			sending_tuning_cmd();
			CLEAR_MDNIE_COMMAND()
			/* remove warning */
			SET_MDNIE_COMMAND(mDNIe_NEGATIVE_MODE_CMDS)
			CLEAR_MDNIE_COMMAND()
		} else {
			SET_MDNIE_COMMAND(mDNIe_NEGATIVE_MODE_CMDS)
			sending_tuning_cmd();
			CLEAR_MDNIE_COMMAND()
		}
	}
}

void is_play_speed_1_5(int enable)
{
	play_speed_1_5 = enable;
}

/* ##########################################################
 * #
 * # MDNIE BG Sysfs node
 * #
 * ##########################################################*/

/* ##########################################################
 * #
 * #	0. Dynamic
 * #	1. Standard
 * #	2. Video
 * #	3. Natural
 * #
 * ##########################################################*/
static ssize_t mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, 256, "Current Background Mode : %s\n",
		background_name[mdnie_tun_state.background]);
}

static ssize_t mode_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	int value;
	struct msm_fb_data_type *mfd;
	mfd = mdnie_msd->mfd;

	sscanf(buf, "%d", &value);
	DPRINT("set background mode : %d\n", value);

	if (value < DYNAMIC_MODE || value >= MAX_BACKGROUND_MODE) {
		DPRINT("[ERROR] wrong backgound mode value : %d\n",
			value);
		return size;
	}

	mdnie_tun_state.background = value;

	if (mdnie_tun_state.negative) {
		DPRINT("already negative mode(%d), do not set background(%d)\n",
			mdnie_tun_state.negative, mdnie_tun_state.background);
	} else {
		DPRINT(" %s, input background(%d)\n",
			__func__, value);
		mDNIe_Set_Mode(mdnie_tun_state.scenario);
	}

	return size;
}

static DEVICE_ATTR(mode, 0664, mode_show, mode_store);

static ssize_t scenario_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	DPRINT("called %s\n", __func__);

	DPRINT("Current Scenario Mode : %s\n",
		scenario_name[mdnie_tun_state.scenario]);

	return snprintf(buf, 256, "Current Scenario Mode : %s\n",
		scenario_name[mdnie_tun_state.scenario]);
}

static ssize_t scenario_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t size)
{
	int value;
	struct msm_fb_data_type *mfd;
	mfd = mdnie_msd->mfd;

	sscanf(buf, "%d", &value);

	if (value < mDNIe_UI_MODE || value >= MAX_mDNIe_MODE) {
		DPRINT("[ERROR] wrong Scenario mode value : %d\n",
			value);
		return size;
	}

	switch (value) {
	case SIG_MDNIE_UI_MODE:
		mdnie_tun_state.scenario = mDNIe_UI_MODE;
		break;

	case SIG_MDNIE_VIDEO_MODE:
		mdnie_tun_state.scenario = mDNIe_VIDEO_MODE;
		break;

	case SIG_MDNIE_VIDEO_WARM_MODE:
		mdnie_tun_state.scenario = mDNIe_VIDEO_WARM_MODE;
		break;

	case SIG_MDNIE_VIDEO_COLD_MODE:
		mdnie_tun_state.scenario = mDNIe_VIDEO_COLD_MODE;
		break;

	case SIG_MDNIE_CAMERA_MODE:
		mdnie_tun_state.scenario = mDNIe_CAMERA_MODE;
		break;

	case SIG_MDNIE_NAVI:
		mdnie_tun_state.scenario = mDNIe_NAVI;
		break;

	case SIG_MDNIE_GALLERY:
		mdnie_tun_state.scenario = mDNIe_GALLERY;
		break;

	case SIG_MDNIE_VT:
		mdnie_tun_state.scenario = mDNIe_VT_MODE;
		break;

	case SIG_MDNIE_BROWSER:
		mdnie_tun_state.scenario = mDNIe_BROWSER_MODE;
		break;

	case SIG_MDNIE_eBOOK:
		mdnie_tun_state.scenario = mDNIe_eBOOK_MODE;
		break;
	case SIG_MDNIE_EMAIL:
		mdnie_tun_state.scenario = mDNIe_EMAIL_MODE;
		break;

#ifdef BROWSER_COLOR_TONE_SET
	case SIG_MDNIE_BROWSER_TONE1:
		mdnie_tun_state.scenario = mDNIe_BROWSER_TONE1;
		break;
	case SIG_MDNIE_BROWSER_TONE2:
		mdnie_tun_state.scenario = mDNIe_BROWSER_TONE2;
		break;
	case SIG_MDNIE_BROWSER_TONE3:
		mdnie_tun_state.scenario = mDNIe_BROWSER_TONE3;
		break;
#endif


#if defined(CONFIG_TDMB)
	case SIG_MDNIE_DMB_MODE:
		mdnie_tun_state.scenario = mDNIe_DMB_MODE;
		break;
	case SIG_MDNIE_DMB_WARM_MODE:
		mdnie_tun_state.scenario = mDNIe_DMB_WARM_MODE;
		break;
	case SIG_MDNIE_DMB_COLD_MODE:
		mdnie_tun_state.scenario = mDNIe_DMB_COLD_MODE;
		break;
#endif

#if defined(CONFIG_MTV)
	case SIG_MDNIE_ISDBT_SOCCERMODE:
		mdnie_tun_state.scenario = mDNIe_ISDBT_SOCCER_MODE;
		break;
#endif

	default:
		DPRINT("scenario_store value is wrong : value(%d)\n",
		       value);
		break;
	}

	if (mdnie_tun_state.negative) {
		DPRINT("already negative mode(%d), do not set mode(%d)\n",
			mdnie_tun_state.negative, mdnie_tun_state.scenario);
	} else {
		DPRINT(" %s, input value = %d\n", __func__, value);
		mDNIe_Set_Mode(mdnie_tun_state.scenario);
	}
	return size;
}
static DEVICE_ATTR(scenario, 0664, scenario_show,
		   scenario_store);

static ssize_t mdnieset_user_select_file_cmd_show(struct device *dev,
						  struct device_attribute *attr,
						  char *buf)
{
	unsigned int mdnie_ui = 0;
	DPRINT("called %s\n", __func__);

	return snprintf(buf, 256, "%u\n", mdnie_ui);
}

static ssize_t mdnieset_user_select_file_cmd_store(struct device *dev,
						   struct device_attribute
						   *attr, const char *buf,
						   size_t size)
{
	int value;

	sscanf(buf, "%d", &value);
	DPRINT
	("inmdnieset_user_select_file_cmd_store, input value = %d\n",
	     value);

	return size;
}

static DEVICE_ATTR(mdnieset_user_select_file_cmd, 0664,
		   mdnieset_user_select_file_cmd_show,
		   mdnieset_user_select_file_cmd_store);

static ssize_t mdnieset_init_file_cmd_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	char temp[] = "mdnieset_init_file_cmd_show\n\0";
	DPRINT("called %s\n", __func__);
	strcat(buf, temp);
	return strlen(buf);
}

static ssize_t mdnieset_init_file_cmd_store(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf, size_t size)
{
	int value;
	struct msm_fb_data_type *mfd;
	mfd = mdnie_msd->mfd;

	sscanf(buf, "%d", &value);
	DPRINT("mdnieset_init_file_cmd_store  : value(%d)\n", value);

	switch (value) {
	case 0:
		mdnie_tun_state.scenario = mDNIe_UI_MODE;
		break;

	default:
		printk(KERN_ERR
		       "mdnieset_init_file_cmd_store value is wrong : value(%d)\n",
		       value);
		break;
	}
	mDNIe_Set_Mode(mdnie_tun_state.scenario);

	return size;
}

static DEVICE_ATTR(mdnieset_init_file_cmd, 0664, mdnieset_init_file_cmd_show,
		   mdnieset_init_file_cmd_store);

static ssize_t outdoor_show(struct device *dev,
					      struct device_attribute *attr,
					      char *buf)
{
	DPRINT("called %s\n", __func__);
	return snprintf(buf, 256, "Current outdoor Value : %s\n",
		(mdnie_tun_state.outdoor == 0) ? "Disabled" : "Enabled");
}

static ssize_t outdoor_store(struct device *dev,
					       struct device_attribute *attr,
					       const char *buf, size_t size)
{
	int value;
	struct msm_fb_data_type *mfd;
	mfd = mdnie_msd->mfd;

	sscanf(buf, "%d", &value);

	DPRINT("outdoor value = %d, scenario = %d\n",
		value, mdnie_tun_state.scenario);

	if (value < OUTDOOR_OFF_MODE || value >= MAX_OUTDOOR_MODE) {
		DPRINT("[ERROR] : wrong outdoor mode value : %d\n",
				value);
	}

	mdnie_tun_state.outdoor = value;

	if (mdnie_tun_state.negative) {
		DPRINT("already negative mode(%d), do not outdoor mode(%d)\n",
			mdnie_tun_state.negative, mdnie_tun_state.outdoor);
	} else {
		mDNIe_Set_Mode(mdnie_tun_state.scenario);
	}

	return size;
}

static DEVICE_ATTR(outdoor, 0664, outdoor_show, outdoor_store);

static ssize_t playspeed_show(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	DPRINT("called %s\n", __func__);
	return snprintf(buf, 256, "%d\n", play_speed_1_5);
}

static ssize_t playspeed_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t size)
{
	int value;
	sscanf(buf, "%d", &value);

	DPRINT("[Play Speed Set]play speed value = %d\n", value);

	is_play_speed_1_5(value);
	return size;
}
static DEVICE_ATTR(playspeed, 0664,
			playspeed_show,
			playspeed_store);

#if defined(CONFIG_CABC_TUNING)
static ssize_t cabc_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int rc;
	unsigned char cabc;
	cabc = mdss_dsi_panel_cabc_show();
	rc = snprintf((char *)buf, sizeof(buf), "%d\n",cabc);
	pr_info("%s : CABC: %d\n", __func__, cabc);
	return rc;

}

static ssize_t cabc_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{

	unsigned char cabc;
	cabc = mdss_dsi_panel_cabc_show();

	if (sysfs_streq(buf, "1") && !cabc)
		cabc = true;
	else if (sysfs_streq(buf, "0") && cabc)
		cabc = false;
	else
		pr_info("%s: Invalid argument!!", __func__);
	mdss_dsi_panel_cabc_store(cabc);

	return size;

}
static DEVICE_ATTR(cabc, 0664, cabc_show, cabc_store);
#endif

static ssize_t negative_show(struct device *dev,
					      struct device_attribute *attr,
					      char *buf)
{
	DPRINT("called %s\n", __func__);
	return snprintf(buf, 256, "Current negative Value : %s\n",
		(mdnie_tun_state.negative == 0) ? "Disabled" : "Enabled");
}

static ssize_t negative_store(struct device *dev,
					       struct device_attribute *attr,
					       const char *buf, size_t size)
{
	int value;

	sscanf(buf, "%d", &value);

	DPRINT
	    ("negative_store, input value = %d\n",
	     value);

	mdnie_tun_state.negative = value;

	mDNIe_set_negative(mdnie_tun_state.negative);
	DPRINT
	    ("negative_store, input value11 = %d\n",
	     value);
	return size;
}

static DEVICE_ATTR(negative, 0664,
		   negative_show,
		   negative_store);
static ssize_t accessibility_show(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	DPRINT("called %s\n", __func__);
	return snprintf(buf, 256, "%d\n", play_speed_1_5);
}

static ssize_t accessibility_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t size)
{
	int cmd_value;
	char buffer[MDNIE_COLOR_BLINDE_CMD] = {0,};
	int buffer2[MDNIE_COLOR_BLINDE_CMD/2] = {0,};
	int loop;
	char temp;
	struct msm_fb_data_type *mfd;
	mfd = mdnie_msd->mfd;

	sscanf(buf, "%d %x %x %x %x %x %x %x %x %x", &cmd_value,
		&buffer2[0], &buffer2[1], &buffer2[2], &buffer2[3], &buffer2[4],
		&buffer2[5], &buffer2[6], &buffer2[7], &buffer2[8]);

	for(loop = 0; loop < MDNIE_COLOR_BLINDE_CMD/2; loop++) {
		buffer2[loop] = buffer2[loop] & 0xFFFF;

		buffer[loop * 2] = (buffer2[loop] & 0xFF00) >> 8;
		buffer[loop * 2 + 1] = buffer2[loop] & 0xFF;
	}

	for(loop = 0; loop < MDNIE_COLOR_BLINDE_CMD; loop+=2) {
		temp = buffer[loop];
		buffer[loop] = buffer[loop + 1];
		buffer[loop + 1] = temp;
	}

	if (cmd_value == NEGATIVE) {
		mdnie_tun_state.negative = mDNIe_NEGATIVE_ON;
		mdnie_tun_state.blind = ACCESSIBILITY_OFF;
	} else if (cmd_value == COLOR_BLIND) {
		mdnie_tun_state.negative = mDNIe_NEGATIVE_OFF;
		mdnie_tun_state.blind = COLOR_BLIND;
	} else if (cmd_value == ACCESSIBILITY_OFF) {
		mdnie_tun_state.blind = ACCESSIBILITY_OFF;
		mdnie_tun_state.negative = mDNIe_NEGATIVE_OFF;
	} else
		pr_info("%s ACCESSIBILITY_MAX", __func__);
	is_negative_on();
	pr_info("%s cmd_value : %d", __func__, cmd_value);

	return size;
}

static DEVICE_ATTR(accessibility, 0664,
			accessibility_show,
			accessibility_store);


static struct class *mdnie_class;
struct device *tune_mdnie_dev;

void init_mdnie_class(void)
{

	DPRINT("start!\n");

	mdnie_class = class_create(THIS_MODULE, "mdnie");
	if (IS_ERR(mdnie_class))
		pr_err("Failed to create class(mdnie)!\n");

	tune_mdnie_dev =
	    device_create(mdnie_class, NULL, 0, NULL,
		  "mdnie");
	if (IS_ERR(tune_mdnie_dev))
		pr_err("Failed to create device(mdnie)!\n");

	if (device_create_file
	    (tune_mdnie_dev, &dev_attr_scenario) < 0)
		pr_err("Failed to create device file(%s)!\n",
	       dev_attr_scenario.attr.name);

	if (device_create_file
	    (tune_mdnie_dev,
	     &dev_attr_mdnieset_user_select_file_cmd) < 0)
		pr_err("Failed to create device file(%s)!\n",
			dev_attr_mdnieset_user_select_file_cmd.attr.name);

	if (device_create_file
	    (tune_mdnie_dev, &dev_attr_mdnieset_init_file_cmd) < 0)
		pr_err("Failed to create device file(%s)!\n",
			dev_attr_mdnieset_init_file_cmd.attr.name);

	if (device_create_file
		(tune_mdnie_dev, &dev_attr_mode) < 0)
		pr_err("Failed to create device file(%s)!\n",
			dev_attr_mode.attr.name);

	if (device_create_file
		(tune_mdnie_dev, &dev_attr_outdoor) < 0)
		pr_err("Failed to create device file(%s)!\n",
	       dev_attr_outdoor.attr.name);

	if (device_create_file
		(tune_mdnie_dev, &dev_attr_playspeed) < 0)
		pr_err("Failed to create device file(%s)!=n",
			dev_attr_playspeed.attr.name);

#if defined(CONFIG_CABC_TUNING)
	if (device_create_file(tune_mdnie_dev, &dev_attr_cabc) < 0) {
		pr_info("[mipi2lvds:ERROR] device_create_file(%s)\n",\
			dev_attr_cabc.attr.name);
	}
#endif

	if (device_create_file
		(tune_mdnie_dev, &dev_attr_accessibility) < 0)
		pr_err("Failed to create device file(%s)!=n",
			dev_attr_accessibility.attr.name);

	if (device_create_file
		(tune_mdnie_dev, &dev_attr_negative) < 0)
		pr_err("Failed to create device file(%s)!\n",
			dev_attr_negative.attr.name);

	mdnie_tun_state.mdnie_enable = true;

	DPRINT("end!\n");
}
void mdnie_tft_init(struct mdss_samsung_driver_data *msd)
{
	mdnie_msd = msd;
	mutex_init(&mdnie_msd->lock);
	mutex_init(&mdnie_lock);
	if(mdnie_msd->manufacture_id == 0x55c090)
		is_ldi_himax = 1;
}