/*
 * Copyright (C) 2012 Spreadtrum Communications Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <mach/gpio.h>
#include <linux/headset_sprd.h>
#include <mach/board.h>
#include <linux/input.h>

#include <mach/adc.h>
#include <asm/io.h>
#include <mach/hardware.h>
#include <mach/adi.h>

#ifdef CONFIG_INPUT_SPRD_HEADSET_USING_THREAD
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#endif /* CONFIG_INPUT_SPRD_HEADSET_USING_THREAD */


#define ADC_FIFO_CNT	(5)
#define HEADSET_TYPE_ADC_DISPART_VALUE	(100 * 4)
#define HEADSET_TYPE_GND_VALUE		(200)

#define HEADMIC_DETECT_BASE	(SPRD_MISC_BASE	+ 0x700)
#define HEADMIC_DETECT_REG(X)   (HEADMIC_DETECT_BASE + (X))

#define HEADMIC_BUTTON_BASE	(SPRD_MISC_BASE	+ 0xe00)
#define HEADMIC_BUTTON_REG(X)   (HEADMIC_BUTTON_BASE + (X))

#define HEADMIC_DETECT_GLB_REG(X)   (ANA_REGS_GLB_BASE + (X))

#define HEADMIC_DETECT_INSRT_VOL_SHIFT  (5)
#define HEADMIC_DETECT_INSRT_VOL_MSK    (0x3 << HEADMIC_DETECT_INSRT_VOL_SHIFT)

#define HEADMIC_DETECT_INSRT_2P1V   (3)
#define HEADMIC_DETECT_INSRT_2P3V   (2)
#define HEADMIC_DETECT_INSRT_2P5V   (1)
#define HEADMIC_DETECT_INSRT_2P7V   (0)


#define HEADMIC_ADC_SWITCH_BIT		(BIT(13))
#define HEADMIC_DETECT_CIRCUIT_BIT      (BIT(11))

#define HEADMIC_DET_ADC_BUF		(BIT(15))
#define HEADMIC_DET_ADC_EN		(BIT(14))

#define ABS(x)	(((x) < (0)) ? (-x) : (x))

#define	headset_reg_read(addr)	\
    do {	\
	sci_adi_read(addr);	\
} while(0)

#define headset_reg_msk_or(val, addr, msk)  \
    do {    \
        uint32_t temp;    \
        temp = sci_adi_read(addr);  \
        temp = (temp & (~msk)) | val;   \
        sci_adi_raw_write(addr, temp);    \
    } while(0)

#define headset_reg_clr_bit(addr, bit)   \
    do {    \
        uint32_t temp;    \
        temp = sci_adi_read(addr);  \
        temp = temp & (~bit);   \
        sci_adi_raw_write(addr, temp);  \
    } while(0)

#define headset_reg_set_bit(addr, bit)   \
    do {    \
        uint32_t temp;    \
        temp = sci_adi_read(addr);  \
        temp = temp | bit;  \
        sci_adi_raw_write(addr, temp);  \
    } while(0)

typedef enum sprd_headset_type{
	HEADSET_NORMAL,
	HEADSET_NO_MIC,
	HEADSET_NORTH_AMERICA,
	HEADSET_APPLE,
	HEADSET_TYPE_MAX,
}SPRD_HEADSET_TYPE;

static DEFINE_SPINLOCK(headmic_button_irq_lock);

#ifdef CONFIG_INPUT_SPRD_HEADSET_USING_THREAD
static struct task_struct *s_headset_thread;
static wait_queue_head_t s_headset_wq;
static int s_headset_event_flags;
#define HEADSET_DETECT 1
#define HEADSET_BUTTON 2
#else  /* CONFIG_INPUT_SPRD_HEADSET_USING_THREAD */
struct workqueue_struct *headset_wq = NULL;
#endif /* CONFIG_INPUT_SPRD_HEADSET_USING_THREAD */

static int headset_timer_period = 500;
static int headset_read_adc_delay = 10;
static int headset_switch_gpio_delay = 30;
static int headset_discard_button_irq = 0;

extern int sprd_codec_headmic_bias_control(int on, int sync);

static BLOCKING_NOTIFIER_HEAD(headset_plug_notify_list);

static struct sprd_headset headset = {
	.detect = {
		.sdev = {
			.name = "h2w",
		}
	},
};

int register_headset_plug_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&headset_plug_notify_list, nb);
}
EXPORT_SYMBOL(register_headset_plug_notifier);

int unregister_headset_plug_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&headset_plug_notify_list, nb);
}
EXPORT_SYMBOL(unregister_headset_plug_notifier);

/*  on = 0: open headmic detect circuit */
static void headset_detect_circuit(unsigned on)
{
    if (on) {
        headset_reg_clr_bit(HEADMIC_DETECT_REG(0xA0), HEADMIC_DETECT_CIRCUIT_BIT);
    } else {
        headset_reg_set_bit(HEADMIC_DETECT_REG(0xA0), HEADMIC_DETECT_CIRCUIT_BIT);
    }
}

static void headset_detect_openclk(void)
{
    headset_reg_set_bit(HEADMIC_DETECT_GLB_REG(0x84), (BIT(14) | BIT(15)));
}

static void headset_detect_init(void)
{
    /* enable headset detect clk */
    headset_reg_set_bit(HEADMIC_DETECT_GLB_REG(0x84), (BIT(14) | BIT(15)));

    headset_reg_set_bit(HEADMIC_DETECT_REG(0xA0), (HEADMIC_DET_ADC_BUF | HEADMIC_DET_ADC_EN));

    /* set headset detect voltage */
    headset_reg_msk_or(HEADMIC_DETECT_INSRT_2P1V, HEADMIC_DETECT_REG(0xA0), HEADMIC_DETECT_INSRT_VOL_MSK);
}

/* is_set = 1, headset_mic to AUXADC */
static void set_adc_to_headmic(unsigned is_set)
{
	if (is_set) {
		headset_reg_set_bit(HEADMIC_DETECT_REG(0xA0), HEADMIC_ADC_SWITCH_BIT);
	} else {
		headset_reg_clr_bit(HEADMIC_DETECT_REG(0xA0), HEADMIC_ADC_SWITCH_BIT);
	}
}

static int headset_read_adc(int* adc_v, int headmic)
{
    int adc_val[ADC_FIFO_CNT] = {0};
    int count;

    headset_detect_init();
    headset_detect_circuit(1);

    set_adc_to_headmic(headmic);
    mdelay(headset_read_adc_delay);

    for (count = 0; count < ADC_FIFO_CNT; count++) {
        adc_val[count] = sci_adc_get_value(ADC_CHANNEL_HEADMIC, 0);
    }
    *adc_v = adc_val[ADC_FIFO_CNT/2];

    pr_debug("%s, adc_mic = %d, headmic = %d\n", __FUNCTION__, *adc_v, headmic);

    return 0;
}

/*
 * for check the adc value in button irq, button irq is also for
 * double check headset type, the ignore some adc to avoid some key event
 */
static int ignore_wrong_button(void)
{
	int adc_mic, adc_l;

        headset_read_adc(&adc_mic, 1);
        headset_read_adc(&adc_l, 0);
	pr_debug("%s, adc_mic = %d, adc_l = %d\n", __FUNCTION__, adc_mic, adc_l);
	if (ABS(adc_mic - adc_l) < HEADSET_TYPE_ADC_DISPART_VALUE) {
		if (ABS(adc_mic - 0) < 500) {
			return 0;
		} else {
			return 1;
		}
	}
	return 0;
}

static SPRD_HEADSET_TYPE detect_headset_type(void)
{
	/* distinguish headset type */
	int adc_mic, adc_l;

        headset_read_adc(&adc_mic, 1);
        headset_read_adc(&adc_l, 0);
	pr_info("%s, adc_mic = %d, adc_l = %d\n", __FUNCTION__, adc_mic, adc_l);
	if (ABS(adc_mic - adc_l) < HEADSET_TYPE_ADC_DISPART_VALUE) {
		if (ABS(adc_mic - 0) < HEADSET_TYPE_GND_VALUE) {
			return HEADSET_NO_MIC;
		} else {
			return HEADSET_NORTH_AMERICA;
		}
	}

	return HEADSET_NORMAL;
}

static void headset_mic_level(int level)
{
	if (level)
		headset_reg_msk_or(0x02, HEADMIC_DETECT_REG(0xA0), 0x1e);
	else
		headset_reg_msk_or(0x16, HEADMIC_DETECT_REG(0xA0), 0x1e);
}

static int headset_button_down(void)
{
	int button_status;
	button_status = sci_adi_read(HEADMIC_DETECT_REG(0xC0)) & (BIT(5) | BIT(6) | BIT(7));
	if (button_status == 0xe0)
		return 1;
	else
		return 0;
}

static int headset_plug_in(void)
{
	int plug_status;
	struct sprd_headset *ht = &headset;
	struct sprd_headset_detect_platform_data *pdata = ht->detect.platform_data;

	plug_status = sci_adi_read(HEADMIC_DETECT_REG(0xC0)) & (BIT(5) | BIT(6));
	if((plug_status == 0x60) && gpio_get_value(pdata->detect_gpio))
		return 1;
	else
		return 0;
}

static void headset_button_irq_enable(int enable, unsigned int irq)
{
    static int button_irq_enable = 1;

    spin_lock(&headmic_button_irq_lock);
    if (!enable) {
        if (button_irq_enable) {
            disable_irq(irq);
            button_irq_enable = 0;
        }
    } else {
        if (!button_irq_enable) {
            enable_irq(irq);
            button_irq_enable = 1;
        }
    }
    spin_unlock(&headmic_button_irq_lock);

    return;
}

static void headset_pwr_on(int pwr_on)
{
    static int pwr_down_flags = 1;

    if (pwr_on) {
        if (pwr_down_flags) {
            sprd_codec_headmic_bias_control(1, 1);
            pwr_down_flags = 0;
        }
    } else {
        if (!pwr_down_flags) {
            sprd_codec_headmic_bias_control(0, 1);
            pwr_down_flags = 1;
        }
    }

    return;
}

//Bug 185497, step 5: Release all buttons  when head set plug out
static void headset_button_release(void)
{
    struct sprd_headset *ht = &headset;
	struct headset_button_data *ht_button = &ht->button;
	struct sprd_headset_buttons_platform_data *pdata = ht_button->platform_data;
       int i;
	for (i = 0; i < pdata->nbuttons; i++) {
        //Bug 185497, Release All pressed buttons 
		if ( HEADSET_BUTTON_STATE_PRESSED == pdata->headset_button[i].state )
		{
			input_event(ht_button->input_dev, EV_KEY, pdata->headset_button[i].code, 0);
			pr_info("%s button = %d, relase\n", __FUNCTION__, pdata->headset_button[i].code);
			input_sync(ht_button->input_dev);
			pdata->headset_button[i].state = HEADSET_BUTTON_STATE_RELASED;
		}	
	
	} 
       
}


static void headset_button_workqueue_func(struct work_struct *work)
{
	struct headset_button_data *ht_button = container_of(work, struct headset_button_data, work);
	struct sprd_headset_buttons_platform_data *pdata = ht_button->platform_data;
	int state;
	int adc_mic;
	int i;
	static int key_code = KEY_RESERVED;

        int ignore_button;
        struct sprd_headset *ht = &headset;

        ignore_button = ignore_wrong_button();
        if (ignore_button) {
            printk("headset ignore wrong button\n");
            mod_timer(&ht->detect.timer, jiffies + msecs_to_jiffies(headset_timer_period));
            return;
        }

        if (!headset_plug_in()) {
            printk("headset ignore wrong button: has been plug out\n");
            return;
        }

        state = headset_button_down();
        headset_read_adc(&adc_mic, 1);
        msleep(80);
	state = state && headset_button_down();
	pr_info("%s adc_mic = %d state = %d\n", __FUNCTION__, adc_mic, state);
	if(state) {
		for (i = 0; i < pdata->nbuttons; i++) {
			if (adc_mic >= pdata->headset_button[i].adc_min && 
					adc_mic < pdata->headset_button[i].adc_max) {
				key_code = pdata->headset_button[i].code;
				break;
			} 
			key_code = KEY_RESERVED;
		}
		input_event(ht_button->input_dev, EV_KEY, key_code, 1);
		
		//Bug 185497, step 2: Record button as pressed
		for (i = 0; i < pdata->nbuttons; i++) {
			if (key_code == pdata->headset_button[i].code) {
				pdata->headset_button[i].state = HEADSET_BUTTON_STATE_PRESSED;
				break;
			} 
		}
	} else {
		input_event(ht_button->input_dev, EV_KEY, key_code, 0);
		
		//Bug 185497, step 3: Record button as relesed
		for (i = 0; i < pdata->nbuttons; i++) {
			if (key_code == pdata->headset_button[i].code) {
				pdata->headset_button[i].state = HEADSET_BUTTON_STATE_RELASED;
				break;
			} 
		}
	}
	input_sync(ht_button->input_dev);
}

static void headset_detect_workqueue_func(struct work_struct *work)
{
	struct headset_detect_data *ht_detect = container_of(work, struct headset_detect_data, work);
	struct sprd_headset *ht = &headset;
	struct sprd_headset_detect_platform_data *pdata = ht->detect.platform_data;
	SPRD_HEADSET_TYPE headset_type;
	int state;

        headset_pwr_on(1);
	state = gpio_get_value(pdata->detect_gpio);//headset_plug_in();

	blocking_notifier_call_chain(&headset_plug_notify_list, state, ht_detect);
	if(state) {
		headset_mic_level(1);
		gpio_direction_output(pdata->switch_gpio, 0);
		mdelay(headset_switch_gpio_delay);

		headset_type = detect_headset_type();
		pr_info("%s headset_type = %d\n", __FUNCTION__, headset_type);

		switch (headset_type) {
		case HEADSET_NORTH_AMERICA:
			gpio_direction_output(pdata->switch_gpio, 1);
			irq_set_irq_type(ht->button.irq, IRQF_TRIGGER_HIGH);
                        mdelay(headset_switch_gpio_delay);
			break;
		case HEADSET_NORMAL:
			irq_set_irq_type(ht->button.irq, IRQF_TRIGGER_HIGH);
			break;
		case HEADSET_NO_MIC:
			irq_set_irq_type(ht->button.irq, IRQF_TRIGGER_LOW);
 		        headset_mic_level(0);
		        headset_pwr_on(0);
			break;
		case HEADSET_APPLE:
		default:
			break;
		}

                if(headset_type == HEADSET_NO_MIC) {
                        ht_detect->headphone = 1;
                        ht_detect->type = BIT_HEADSET_NO_MIC;
                        switch_set_state(&ht_detect->sdev, ht_detect->type);
                        pr_info("headphone plug in\n");
                }
                else {
                        ht_detect->headphone = 0;
                        ht_detect->type = BIT_HEADSET_MIC;
                        switch_set_state(&ht_detect->sdev, ht_detect->type);
                        headset_button_irq_enable(1, ht->button.irq);
                        pr_info("headset plug in\n");
                }
	} else {
		headset_pwr_on(0);

                if (ht_detect->headphone) {
			
			pr_info("headphone plug out\n");
		}
		else {
			pr_info("headset plug out\n");
		}
		ht_detect->type = BIT_HEADSET_OUT;
		switch_set_state(&ht_detect->sdev, ht_detect->type);

                //Bug 185497,  step 4: Release all headset buttons  when head set plug out
                headset_button_release();
	}
}

#ifdef CONFIG_INPUT_SPRD_HEADSET_USING_THREAD
static int headset_thread(void *data)
{
        struct sched_param param = {.sched_priority = 2};
        sched_setscheduler(current, SCHED_FIFO, &param);
        printk(KERN_INFO "headset_thread enter\r\n");

        while (!kthread_should_stop())
        {
                int flags = 0;

                wait_event(s_headset_wq,  s_headset_event_flags);
                flags = s_headset_event_flags;
                s_headset_event_flags =  0;

                pr_debug("headset_thread event_flags = %d\n", flags);
                switch (flags) {
                        case HEADSET_DETECT:
                                headset_detect_workqueue_func(&(headset.detect.work));
                                break;
                        case HEADSET_BUTTON:
                                headset_button_workqueue_func(&(headset.button.work));
                                break;
                        default:
                                printk("headset default\n");
                                break;
                }
        }

        return 0;
}

int headset_wake_up_headset_thread(int even_flag)
{
        /*
         * If signal HEADSET_DETECT has't been report to thread,
         * we can't using HEADSET_BUTTON to wake up thread,
         * other else, we should loss the event HEADSET_DETECT.
         * */
        if ((HEADSET_BUTTON == even_flag) && (HEADSET_DETECT == s_headset_event_flags))
        {
                headset_discard_button_irq++;
                return 0;
        }

        s_headset_event_flags = even_flag;
        wake_up(&s_headset_wq);

        return s_headset_event_flags;
}
#endif /* CONFIG_INPUT_SPRD_HEADSET_USING_THREAD */

static irqreturn_t headset_button_irq_handler(int irq, void *dev)
{
	struct sprd_headset *ht = dev;

	if (!headset_plug_in())
		return IRQ_HANDLED;

	if (!ht->button.platform_data)
		return IRQ_HANDLED;

	if (ht->detect.platform_data->button_active_low == 1) {
		irq_set_irq_type(ht->button.irq, IRQF_TRIGGER_LOW);
		ht->detect.platform_data->button_active_low = 0;
	} else {
		irq_set_irq_type(ht->button.irq, IRQF_TRIGGER_HIGH);
		ht->detect.platform_data->button_active_low = 1;
	}

#ifdef CONFIG_INPUT_SPRD_HEADSET_USING_THREAD
        headset_wake_up_headset_thread(HEADSET_BUTTON);
#else  /* CONFIG_INPUT_SPRD_HEADSET_USING_THREAD */
        if (headset_wq != NULL)
            queue_work(headset_wq, &ht->button.work);
#endif /* CONFIG_INPUT_SPRD_HEADSET_USING_THREAD */

	return IRQ_HANDLED;
}

static irqreturn_t headset_detect_irq_handler(int irq, void *dev)
{
	struct sprd_headset *ht = dev;

	if(ht->detect.platform_data->detect_active_low == 1) {
		irq_set_irq_type(ht->detect.irq, IRQF_TRIGGER_LOW);
		ht->detect.platform_data->detect_active_low = 0;
	} else {
		irq_set_irq_type(ht->detect.irq, IRQF_TRIGGER_HIGH);
		ht->detect.platform_data->detect_active_low = 1;		
	}
        /*
         * Maybe, we should disable button irq when receive detect irq,
         * we can enable button irq after report headset plug in event.
         * */
        headset_button_irq_enable(0, ht->button.irq);

	mod_timer(&ht->detect.timer,
			jiffies + msecs_to_jiffies(headset_timer_period));
	return IRQ_HANDLED;
}

static void headset_detect_timer(unsigned long _data)
{
	struct sprd_headset *data = (struct sprd_headset *)_data;

#ifdef CONFIG_INPUT_SPRD_HEADSET_USING_THREAD
        headset_wake_up_headset_thread(HEADSET_DETECT);
#else  /* CONFIG_INPUT_SPRD_HEADSET_USING_THREAD */
        if (headset_wq != NULL)
            queue_work(headset_wq, &data->detect.work);
#endif /* CONFIG_INPUT_SPRD_HEADSET_USING_THREAD */
}

static __devinit int headset_detect_probe(struct platform_device *pdev)
{
	struct sprd_headset_detect_platform_data *pdata = pdev->dev.platform_data;
	struct device *dev = &pdev->dev;
	struct sprd_headset *ht = &headset;
	unsigned long irqflags;
	int error;

	error = switch_dev_register(&ht->detect.sdev);
	if (error < 0) {
		pr_err("switch_dev_register failed!\n");
		return error;
	}

	ht->detect.platform_data = pdata;
	
	setup_timer(&ht->detect.timer, headset_detect_timer, (unsigned long)ht);
#ifdef CONFIG_INPUT_SPRD_HEADSET_USING_THREAD
        wake_up_process(s_headset_thread);
#else  /* CONFIG_INPUT_SPRD_HEADSET_USING_THREAD */
        INIT_WORK(&ht->detect.work, headset_detect_workqueue_func);
#endif /* CONFIG_INPUT_SPRD_HEADSET_USING_THREAD */

	error = gpio_request(pdata->switch_gpio, "headset_switch");
	if (error < 0) {
		dev_err(dev, "failed to request GPIO %d, error %d\n",
			pdata->switch_gpio, error);
		goto fail1;
	}

	error = gpio_request(pdata->detect_gpio, "headset_detect");
	if (error < 0) {
		dev_err(dev, "failed to request GPIO %d, error %d\n",
			pdata->detect_gpio, error);
		goto fail1;
	}

	error = gpio_request(pdata->button_gpio, "headset_button");
	if (error < 0) {
		dev_err(dev, "failed to request GPIO %d, error %d\n",
			pdata->button_gpio, error);
		goto fail1;
	}

	error = gpio_direction_input(pdata->detect_gpio);
	if (error < 0) {
		dev_err(dev, "failed to configure"
			" direction for GPIO %d, error %d\n",
			pdata->detect_gpio, error);
		gpio_free(pdata->detect_gpio);
		goto fail1;
	}

	ht->button.irq = gpio_to_irq(pdata->button_gpio);
	if (ht->button.irq < 0) {
		error = ht->button.irq;
		dev_err(dev, "Unable to get irq number for GPIO %d, error %d\n",
			pdata->button_gpio, error);
		goto fail1;
	}

	irqflags = pdata->button_active_low ? IRQF_TRIGGER_HIGH : IRQF_TRIGGER_LOW;
	error = request_irq(ht->button.irq, headset_button_irq_handler,
						irqflags, "headset_button", ht);
	if (error) {
		dev_err(dev, "Failed to register interrupt\n");
		goto fail1;
	}

	/* disable button irq before headset detected*/
	headset_button_irq_enable(0, ht->button.irq);

	ht->detect.irq = gpio_to_irq(pdata->detect_gpio);
	if (ht->detect.irq < 0) {
		error = ht->detect.irq;
		dev_err(dev, "Unable to get irq number for GPIO %d, error %d\n",
			pdata->detect_gpio, error);
		gpio_free(pdata->detect_gpio);
		goto fail1;
	}

	irqflags = pdata->detect_active_low ? IRQF_TRIGGER_HIGH : IRQF_TRIGGER_LOW;

	error = request_irq(ht->detect.irq, headset_detect_irq_handler,
							irqflags, "headset_detect", ht);
	if (error < 0) {
		dev_err(dev, "%s: request irq failed %d\n", __FUNCTION__, error);
		goto fail1;
	}

	return 0;

fail1:
	return error;
}

static __devinit int headset_buttons_probe(struct platform_device *pdev)
{
	struct sprd_headset_buttons_platform_data *pdata = pdev->dev.platform_data;
	struct device *dev = &pdev->dev;
	struct sprd_headset *ht = &headset;
	struct input_dev *input_dev;
	int i, error;

	input_dev = input_allocate_device();
	if (!pdata || !input_dev) {
		dev_err(dev, "failed to allocate state\n");
		error = -ENOMEM;
		goto fail1;
	}
	input_dev->name = "headset-keyboard";
	input_dev->id.bustype = BUS_HOST;

	ht->button.platform_data = pdata;
	ht->button.input_dev = input_dev;

#ifndef CONFIG_INPUT_SPRD_HEADSET_USING_THREAD
	INIT_WORK(&ht->button.work, headset_button_workqueue_func);
#endif /* CONFIG_INPUT_SPRD_HEADSET_USING_THREAD */

	for (i = 0; i < pdata->nbuttons; i++) {
		struct headset_button *button = &pdata->headset_button[i];
		unsigned int type = button->type ?: EV_KEY;
		input_set_capability(input_dev, type, button->code);
		//Bug 185497, step 1: Button state should be relesed as defalut
		button->state = HEADSET_BUTTON_STATE_RELASED;
	}

	error = input_register_device(input_dev);
	if (error) {
		goto fail1;
	}

	return 0;

fail1:
	input_free_device(input_dev);
	return error;

}
static int headset_suspend(struct platform_device * pdev, pm_message_t state)
{
	pr_info("%s\n", __FUNCTION__);

//	headset_irq_enable(0, headset.detect.irq);
	headset_reg_msk_or(0x01, HEADMIC_BUTTON_REG(0x34), 0x01);
	headset_reg_msk_or(0x32, HEADMIC_BUTTON_REG(0x3c), 0x0f);
	headset_reg_msk_or(0xff, HEADMIC_BUTTON_REG(0x40), 0xffff);
	headset_reg_msk_or(0xff, HEADMIC_BUTTON_REG(0x44), 0xffff);

	msleep(100);
	headset_reg_clr_bit(HEADMIC_DETECT_REG(0x40), BIT(5));
	//headset_reg_set_bit(HEADMIC_DETECT_REG(0x40), BIT(1));

	return 0;
}
static int headset_resume(struct platform_device * pdev)
{
	pr_info("%s\n", __FUNCTION__);

//	headset_irq_enable(1, headset.detect.irq);
	//headset_reg_clr_bit(HEADMIC_DETECT_REG(0x40), BIT(1));
	headset_reg_set_bit(HEADMIC_DETECT_REG(0x40), BIT(5));
	headset_reg_clr_bit(HEADMIC_BUTTON_REG(0x34), 0x01);
	headset_reg_clr_bit(HEADMIC_BUTTON_REG(0x3c), 0x0f);
	headset_reg_clr_bit(HEADMIC_BUTTON_REG(0x40), 0xffff);
	headset_reg_clr_bit(HEADMIC_BUTTON_REG(0x44), 0xffff);

	return 0;
}

static struct platform_driver headset_detect_driver = {
	.driver = {
		.name = "headset-detect",
		.owner = THIS_MODULE,
	},
	.probe = headset_detect_probe,
#if 0
        .suspend = headset_suspend,
	.resume = headset_resume,
#endif
};

static struct platform_driver headset_buttons_driver = {
	.driver = {
		.name = "headset-buttons",
		.owner = THIS_MODULE,
	},
	.probe = headset_buttons_probe,
};

static int __init headset_init(void)
{
	int ret;
#ifdef CONFIG_INPUT_SPRD_HEADSET_USING_THREAD
        init_waitqueue_head(&s_headset_wq);
        s_headset_thread = kthread_create(headset_thread, NULL, "headset_thread");

        if (IS_ERR(s_headset_thread)) {
            printk("headset:create headset_thread error!.\n");
            return -1;
        }
#else  /* CONFIG_INPUT_SPRD_HEADSET_USING_THREAD */
        headset_wq = create_singlethread_workqueue("headset_wq");
#endif /* CONFIG_INPUT_SPRD_HEADSET_USING_THREAD */

        ret = platform_driver_register(&headset_detect_driver);
	ret |= platform_driver_register(&headset_buttons_driver);
	return ret;
}

static void __exit headset_exit(void)
{
#ifdef CONFIG_INPUT_SPRD_HEADSET_USING_THREAD
        if (s_headset_thread != NULL) {
                kthread_stop(s_headset_thread);
                s_headset_thread = NULL;
        }
#else  /* CONFIG_INPUT_SPRD_HEADSET_USING_THREAD */
        if (headset_wq != NULL) {
                flush_workqueue(headset_wq);
                destroy_workqueue(headset_wq);
                headset_wq = NULL;
        }
#endif /* CONFIG_INPUT_SPRD_HEADSET_USING_THREAD */

	platform_driver_unregister(&headset_buttons_driver);
	platform_driver_unregister(&headset_detect_driver);
}

module_param_named(headset_timer_period, headset_timer_period, int, S_IRUGO | S_IWUSR);
module_param_named(headset_read_adc_delay, headset_read_adc_delay, int, S_IRUGO | S_IWUSR);
module_param_named(headset_switch_gpio_delay, headset_switch_gpio_delay, int, S_IRUGO | S_IWUSR);
module_param_named(headset_discard_button_irq, headset_discard_button_irq, int, S_IRUGO | S_IWUSR);

module_init(headset_init);
module_exit(headset_exit);

MODULE_DESCRIPTION("headset & button detect driver");
MODULE_LICENSE("GPL");
