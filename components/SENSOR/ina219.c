#include "ina219.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "iic.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_err.h"
#include "string.h"
#include "math.h"

static const char *TAG = "INA219";

static SemaphoreHandle_t ina_mutex = NULL;
static bool s_ina219_present = false;        /* 芯片是否在线 */
static bool s_ina_fault_reported = false;    /* 故障是否已报过(只报一次) */

/* 快速探测 INA219 是否存在(上电时用一次) */
static bool ina219_probe(void)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, INA219_WR_ADDR, true);
    i2c_master_write_byte(cmd, INA219_CONFIG, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);

    if (ret != ESP_OK) {
        return false;
    }

    /* 再读两字节确认 */
    uint8_t data_high = 0, data_low = 0;
    cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, INA219_RD_ADDR, true);
    i2c_master_read_byte(cmd, &data_high, I2C_MASTER_ACK);
    i2c_master_read_byte(cmd, &data_low, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);

    return (ret == ESP_OK);
}

void INA219_send_cmd(uint8_t cmd_data, uint8_t data_high, uint8_t data_low)
{
    if (!s_ina219_present) {
        if (!s_ina_fault_reported) {
            s_ina_fault_reported = true;
            ESP_LOGW(TAG, "INA219 硬件未就绪，跳过配置写入");
        }
        return;
    }

    xSemaphoreTake(ina_mutex, portMAX_DELAY);
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, INA219_WR_ADDR, true);
    i2c_master_write_byte(cmd, cmd_data, true);
    i2c_master_write_byte(cmd, data_high, true);
    i2c_master_write_byte(cmd, data_low, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "INA写失败: %s", esp_err_to_name(ret));
        s_ina219_present = false;
        s_ina_fault_reported = true;
    }
    xSemaphoreGive(ina_mutex);
}

int16_t INA219_read_data(uint8_t cmd_data)
{
    if (!s_ina219_present) {
        if (!s_ina_fault_reported) {
            s_ina_fault_reported = true;
            ESP_LOGW(TAG, "INA219 硬件未就绪，跳过读取（仅提示一次）");
        }
        return 0;
    }

    xSemaphoreTake(ina_mutex, portMAX_DELAY);
    uint8_t data_high = 0, data_low = 0;
    esp_err_t ret;

    i2c_cmd_handle_t cmd_write = i2c_cmd_link_create();
    i2c_master_start(cmd_write);
    i2c_master_write_byte(cmd_write, INA219_WR_ADDR, true);
    i2c_master_write_byte(cmd_write, cmd_data, true);
    i2c_master_stop(cmd_write);
    ret = i2c_master_cmd_begin(I2C_NUM_0, cmd_write, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd_write);
    if (ret != ESP_OK)
    {
        s_ina219_present = false;
        ESP_LOGW(TAG, "INA写指针失败: %s（后续静默）", esp_err_to_name(ret));
        xSemaphoreGive(ina_mutex);
        return 0;
    }

    i2c_cmd_handle_t cmd_read = i2c_cmd_link_create();
    i2c_master_start(cmd_read);
    i2c_master_write_byte(cmd_read, INA219_RD_ADDR, true);
    i2c_master_read_byte(cmd_read, &data_high, I2C_MASTER_ACK);
    i2c_master_read_byte(cmd_read, &data_low, I2C_MASTER_NACK);
    i2c_master_stop(cmd_read);
    ret = i2c_master_cmd_begin(I2C_NUM_0, cmd_read, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd_read);
    if (ret != ESP_OK)
    {
        s_ina219_present = false;
        ESP_LOGW(TAG, "INA读数据失败: %s（后续静默）", esp_err_to_name(ret));
        xSemaphoreGive(ina_mutex);
        return 0;
    }

    /* 读写成功 → 在线, 复位故障标志 */
    s_ina219_present = true;
    s_ina_fault_reported = false;
    xSemaphoreGive(ina_mutex);
    return (int16_t)((data_high << 8) | data_low);
}

void INA219_init(void)
{
    ina_mutex = xSemaphoreCreateMutex();

    if (!ina219_probe()) {
        s_ina219_present = false;
        s_ina_fault_reported = true;
        ESP_LOGW(TAG, "INA219 探测失败！I2C 地址 0x%02X 无应答，后续跳过所有 INA 读写", INA219_ADDR_7BIT);
        return;
    }

    s_ina219_present = true;
    s_ina_fault_reported = false;

    /* 1. 软件复位 */
    INA219_send_cmd(INA219_CONFIG, 0x80, 0X00);
    vTaskDelay(pdMS_TO_TICKS(20));
    /* 2. 32V量程、±320mV PGA、12bit×1次采样(532us)、连续测量
     *    SADC 必须单次采样: 平均模式(128次=68ms)会把上升沿/峰值抹平 */
    INA219_send_cmd(INA219_CONFIG, 0x39, 0x9F);
    /* 3. 校准寄存器 (CAL=0x0333, 电流 LSB=1mA, 与 CURRENT_LSB 一致; 硬件实测标定, 勿改) */
    INA219_send_cmd(INA219_CALIBR, 0x03, 0X33);

    ESP_LOGI(TAG, "INA219 配置成功");
    vTaskDelay(pdMS_TO_TICKS(250));
}

float ina219_get_shunt_voltage(void)
{
    int16_t raw = INA219_read_data(INA219_SHUNT);
    return raw * 10.0f / 1000000.0f;
}

float ina219_get_valve_voltage(void)
{
    float vbus = ina219_get_bus_voltage();
    float shunt_v = ina219_get_shunt_voltage();
    return vbus - shunt_v;
}

float ina219_get_bus_voltage(void)
{
    int16_t raw = INA219_read_data(INA219_BUS);
    uint16_t raw_u = (uint16_t)raw;
    uint16_t valid = raw_u >> 3;
    return valid * 4.0f / 1000.0f;
}

float ina219_get_current(void)
{
    int16_t raw = INA219_read_data(INA219_CURRENT);
    return raw * CURRENT_LSB;
}

float ina219_get_power(void)
{
    int16_t raw = INA219_read_data(INA219_POWER);
    return raw * 20.0f * CURRENT_LSB;
}

/* 阀门电流测量: 开阀瞬间起连续采样(每点带时间戳), 自适应停止后测 IP/IH/TP 与纹波 period/duty.
 * period/duty 测不出(负载变化快于 INA219 采样能力等)时置 ripple_err 错误码, 见 RIPPLE_ERR_* */

#define MIN_MEASURE_MS        300     /* 最短测量时长 */
#define MAX_MEASURE_MS        5000    /* 超时保护, 异常时自动关阀防线圈过流 */
#define IH_STABLE_MS          400     /* 电流连续处于 IH 区间 ≥400ms 认为测量完成 */
#define IH_ENTER_RATIO        0.50f   /* 进入 IH 区间: 电流 < 50%Ip */
#define IH_MIN_IP_A           0.01f   /* Ip ≥10mA 才进入 IH 判定 */
#define NO_PEAK_ABORT_MS      1000    /* 1s 无有效峰值(<20mA) → 提前结束 */
#define NO_PEAK_ABORT_A       0.02f   /* 有效峰值下限 */

#define FAST_RECORD_MS        80      /* 开阀后 80ms 逐点记录(抓上升沿) */
#define RIPPLE_DENSE_MS       500     /* 纹波密集采样窗口: IH 段 1ms 密集已覆盖主要纹波, dense 仅补尾部
                                         (缩短以控制总记录<1024, 防止覆盖 fast/TP 段导致 TP 测不出) */    /* 纹波密集采样窗口: PWM 在 IP 结束后立即出现, IH 段 1ms 密集已覆盖
                                         (3000ms 会使总记录 1947>1024 环形容量, 覆盖掉 fast 段的 IP 上升沿,
                                         导致 TP 测不出, 故改回 1000ms) */
#define DENSE_RECORD_INTERVAL_US 1000 /* 密集段记录间隔 1ms(抽样): 1000ms≈1000点, 每 120ms 周期约 120 点 */
#define RIPPLE_MAX_PERIOD_US  500000  /* 可测周期上限(µs): 窗口内至少 2 个完整周期才可靠
                                         (旧值 100000 会把 120ms=120000µs 的周期直接过滤掉) */
#define CHANGE_RECORD_A       0.005f  /* 电流变化 ≥5mA 记录一点 */
#define CHANGE_RECORD_MIN_US  2000    /* 变化点最小间隔 2ms: 抑制大电流波动段(TP段)的记录风暴,
                                         防止环形缓冲覆盖 fast/TP 段导致 TP 测不出 */
#define HEARTBEAT_MS          50      /* 平稳段最长 50ms 记录一点 */
#define REC_CAPACITY          1024    /* 记录容量 (16B/条, 堆分配 16KB, 勿加大: 32KB 大块分配易失败) */

#define IP_EDGE_RATIO         0.60f   /* ≥60%Ip 视为 IP 保持段 */
#define IH_TAIL_MS            500     /* IH 兜底: 取测量末尾 500ms */

#define RIPPLE_MIN_AMP_RATIO  0.03f   /* 纹波幅度 <3%Ih 判为无纹波 */
#define RIPPLE_MIN_RISE_CNT   2       /* 至少 2 个上升沿(1 个完整周期)即可出周期值 */

typedef struct {
    int64_t t_us;   /* esp_timer 时间戳(us) */
    float   cur;    /* 电流 (A) */
} ina_rec_t;

/* 环形缓冲: k 为时间顺序下标(0=最早), 满时覆盖最旧 */
#define REC_AT(k)  rec[((rec_start + (k)) % REC_CAPACITY)]

/* 记录点 a→b 之间电流首次跨过 thr 的时刻(us), 线性插值 */
static int64_t ina_interp_cross_us(const ina_rec_t *a, const ina_rec_t *b, float thr)
{
    float delta = b->cur - a->cur;
    if (fabsf(delta) < 1e-6f) {
        return a->t_us;
    }
    float k = (thr - a->cur) / delta;
    return a->t_us + (int64_t)((float)(b->t_us - a->t_us) * k);
}

esp_err_t ina219_measure_valve_current(ina219_valve_curr_t *res,
                                        void (*valve_open_func)(void),
                                        void (*valve_close_func)(void))
{
    if (res == NULL || valve_open_func == NULL || valve_close_func == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(res, 0, sizeof(ina219_valve_curr_t));

    if (!s_ina219_present) {
        ESP_LOGW(TAG, "INA219 不在线，跳过阀门电流测量");
        return ESP_ERR_NOT_SUPPORTED;
    }

    ina_rec_t *rec = (ina_rec_t *)malloc(REC_CAPACITY * sizeof(ina_rec_t));
    if (rec == NULL) {
        ESP_LOGE(TAG, "采样记录内存分配失败");
        return ESP_ERR_NO_MEM;
    }

    /* 初始关闭, 等电流泄放稳定 */
    valve_close_func();
    vTaskDelay(pdMS_TO_TICKS(100));

    /* 关阀基线: 判断 24V 供电是否到测量点 */
    float vbus_off = ina219_get_bus_voltage();
    float i_base = ina219_get_current();
    ESP_LOGI(TAG, "[诊断] 关阀基线: VBUS=%.2fV I=%.3fA P=%.2fW",
             vbus_off, i_base, ina219_get_power());
    if (vbus_off < 5.0f) {
        ESP_LOGW(TAG, "[诊断] VBUS过低(<5V), 24V供电可能未到INA219测量点!");
    }

    /* 开阀瞬间起连续采样, 自适应时长 */
    int64_t t0 = esp_timer_get_time();
    valve_open_func();
    ESP_LOGI(TAG, "开阀指令已发出, 从 t=0 开始连续采样 (自适应停止) ...");

    int   rec_cnt      = 0;
    int   rec_start    = 0;
    float i_prev       = i_base;
    int64_t t_prev     = t0;
    float i_max        = 0.0f;
    int64_t t_max      = t0;
    bool full_warned   = false;
    int samples_since_yield = 0;

    int64_t t_fast_end         = t0 + FAST_RECORD_MS * 1000;
    int64_t t_min_end          = t0 + MIN_MEASURE_MS * 1000;
    int64_t t_max_end          = t0 + MAX_MEASURE_MS * 1000;
    int64_t t_no_peak_end      = t0 + NO_PEAK_ABORT_MS * 1000;
    int64_t t_ih_sustain_start = -1;    /* 当前连续处于 IH 区间的起点, -1=不在 */
    bool    ih_entered         = false; /* 是否已进入 IH 区间(TP结束): 进入即开始密集记录, 保证保持初段的纹波不漏 */
    int64_t t_ih_enter         = -1;    /* 首次进入 IH 的时刻: 纹波分析窗口起点 */
    bool    dense_burst        = false; /* 停止判定通过后密集采样做纹波检测 */
    int64_t t_dense_start      = 0;
    int64_t t_dense_end        = 0;
    const char *stop_reason    = "IH保持稳定(自适应)";

    while (1) {
        int64_t t = esp_timer_get_time();
        float i = ina219_get_current();
        samples_since_yield++;

        /* 全程逐点跟踪峰值 */
        if (i > i_max) {
            i_max = i;
            t_max = t;
        }

        /* 记录判定: 首点/快速段逐点; 进入 IH 后(TP结束)按 1ms 密集记录(纹波多出现在保持初段,
         * 不能再等稳定后才密集); dense 段(稳定后)按 2ms 抽样省容量; 大变化点(≥5mA 且间隔≥2ms), 心跳保底 */
        bool record = (rec_cnt == 0)
                   || (t <= t_fast_end)
                   || (ih_entered && !dense_burst && (t - t_prev) >= DENSE_RECORD_INTERVAL_US)
                   || (dense_burst && (t - t_prev) >= DENSE_RECORD_INTERVAL_US * 2)
                   || (fabsf(i - i_prev) >= CHANGE_RECORD_A && (t - t_prev) >= CHANGE_RECORD_MIN_US)
                   || ((t - t_prev) >= HEARTBEAT_MS * 1000);

        if (record) {
            ina_rec_t *dst = &REC_AT(rec_cnt);
            dst->t_us = t;
            dst->cur  = i;
            if (rec_cnt < REC_CAPACITY) {
                rec_cnt++;
            } else {
                rec_start = (rec_start + 1) % REC_CAPACITY;   /* 满则覆盖最旧 */
            }
            if (rec_cnt == REC_CAPACITY && !full_warned) {
                full_warned = true;
                ESP_LOGI(TAG, "记录缓冲已满(%d), 覆盖最早记录, 保留末尾纹波段", REC_CAPACITY);
            }
            i_prev = i;
            t_prev = t;
        }

        /* 停止判定: 峰值下降后电流连续处于 IH 区间 ≥400ms → 完成; 最长 5s 超时保护 */
        bool in_ih = (i_max >= IH_MIN_IP_A) && (i < i_max * IH_ENTER_RATIO);
        if (in_ih) {
            if (t_ih_sustain_start < 0) {
                t_ih_sustain_start = t;
            }
            if (!ih_entered) {
                ih_entered = true;
                t_ih_enter = t;
            }
        } else {
            t_ih_sustain_start = -1;
        }

        if (!dense_burst) {
            bool no_peak = (t >= t_no_peak_end) && (i_max < NO_PEAK_ABORT_A);
            bool stop_req = (t >= t_min_end)
                         && ((t_ih_sustain_start > 0
                              && (t - t_ih_sustain_start) >= IH_STABLE_MS * 1000)
                          || (t >= t_max_end)
                          || no_peak);
            if (stop_req) {
                if (no_peak) {
                    stop_reason = "未检测到峰值电流";
                    break;
                }
                if (t >= t_max_end) {
                    stop_reason = "达到最大测量时长(超时保护)";
                }
                dense_burst = true;
                t_dense_start = t;
                t_dense_end = t + RIPPLE_DENSE_MS * 1000;
                ESP_LOGI(TAG, "测量完成判定触发(%s), 再密集采样 %dms 检测纹波 ...",
                         stop_reason, RIPPLE_DENSE_MS);
            }
        }

        if (dense_burst && t >= t_dense_end) {
            break;
        }

        /* 中段让出 CPU, 避免长时间抢占网络任务 */
        if (!dense_burst && t > t_fast_end && samples_since_yield >= 25) {
            samples_since_yield = 0;
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    int64_t t_end = esp_timer_get_time();
    valve_close_func();
    ESP_LOGI(TAG, "采样完成(%s): %d 条记录, 总耗时 %lldms, 全程峰值 I=%.3fA @%lldms",
             stop_reason, rec_cnt, (long long)((t_end - t0) / 1000),
             (double)i_max, (long long)((t_max - t0) / 1000));

    if (i_max < 0.01f) {
        ESP_LOGW(TAG, "未检测到明显峰值电流 (I_max=%.3fA), 请检查供电/驱动", (double)i_max);
    }

    /* Step 1: IP = 全程最大电流 */
    res->Ip = i_max;

    /* Step 2: IH = 稳定达成后(dense段)均值 —— 排除 IP 结束后的 0.1A 级过渡平台,
     * 只统计真正的维持段; 未进入 IH 则兜底取末尾 500ms 平均 */
    int64_t ih_start = (t_dense_start > t0) ? t_dense_start : t_ih_sustain_start;
    if (ih_start > 0) {
        float ih_sum = 0.0f;
        int   ih_cnt = 0;
        for (int j = 0; j < rec_cnt; j++) {
            if (REC_AT(j).t_us >= ih_start) {
                ih_sum += REC_AT(j).cur;
                ih_cnt++;
            }
        }
        if (ih_cnt > 0) {
            res->Ih = ih_sum / (float)ih_cnt;
        }
    } else {
        int64_t t_tail_start = t_end - IH_TAIL_MS * 1000;
        float ih_sum = 0.0f;
        int   ih_cnt = 0;
        for (int j = 0; j < rec_cnt; j++) {
            if (REC_AT(j).t_us >= t_tail_start && REC_AT(j).cur < i_max * 0.5f) {
                ih_sum += REC_AT(j).cur;
                ih_cnt++;
            }
        }
        if (ih_cnt == 0) {   /* 兜底: 尾段全部点平均 */
            for (int j = 0; j < rec_cnt; j++) {
                if (REC_AT(j).t_us >= t_tail_start) {
                    ih_sum += REC_AT(j).cur;
                    ih_cnt++;
                }
            }
        }
        if (ih_cnt > 0) {
            res->Ih = ih_sum / (float)ih_cnt;
        }
    }

    /* Step 3: TP = 60%Ip 上升沿 → 90%Ip 下降沿 (线性插值) */
    float ip_edge = i_max * IP_EDGE_RATIO;

    int rise_j = -1;
    for (int j = 0; j < rec_cnt; j++) {
        if (REC_AT(j).cur >= ip_edge) {
            rise_j = j;
            break;
        }
    }

    int fall_j = -1;
    int64_t t_rise = t0;
    int64_t t_fall = t_end;

    if (rise_j < 0) {
        ESP_LOGW(TAG, "未找到 60%%Ip 上升沿, TP 置 0 (IP=%.3fA)", (double)i_max);
    } else {
        if (rise_j > 0) {
            t_rise = ina_interp_cross_us(&REC_AT(rise_j - 1), &REC_AT(rise_j), ip_edge);
        } else {
            t_rise = REC_AT(0).t_us;
        }

        for (int j = 1; j < rec_cnt; j++) {
            if (REC_AT(j - 1).cur >= ip_edge && REC_AT(j).cur < ip_edge) {
                fall_j = j;
                break;
            }
        }

        if (fall_j > 0) {
            t_fall = ina_interp_cross_us(&REC_AT(fall_j - 1), &REC_AT(fall_j), ip_edge);
        } else {
            ESP_LOGW(TAG, "未找到 90%%Ip 下降沿, 用测量末尾估算 TP");
        }

        res->Tp = (t_fall > t_rise) ? (t_fall - t_rise) : 0;
    }

    ESP_LOGI(TAG, "TP=%lldus(%.1fms) | 90%%Ip 起点=%.1fms 终点=%.1fms",
             (long long)res->Tp, (double)res->Tp / 1000.0,
             (double)(t_rise - t0) / 1000.0, (double)(t_fall - t0) / 1000.0);

    /* Step 4: uptime = 开阀→90%Ip, downtime = 90%Ip→(Ip+Ih)/2 */
    int64_t t_ih_cross = t_end;
    if (fall_j > 0) {
        float ih_mid = (i_max + res->Ih) * 0.5f;
        int mid_j = -1;
        for (int j = fall_j; j < rec_cnt; j++) {
            if (REC_AT(j).cur <= ih_mid) {
                mid_j = j;
                break;
            }
        }
        if (mid_j > 0) {
            t_ih_cross = ina_interp_cross_us(&REC_AT(mid_j - 1), &REC_AT(mid_j), ih_mid);
        }
    }
    int64_t uptime_us   = (t_rise > t0) ? (t_rise - t0) : 0;
    int64_t downtime_us = (t_ih_cross > t_fall) ? (t_ih_cross - t_fall) : 0;

    ESP_LOGI(TAG, "IP=%.3fA | IH=%.3fA | uptime≈%.1fms downtime≈%.1fms",
             (double)res->Ip, (double)res->Ih,
             (double)uptime_us / 1000.0, (double)downtime_us / 1000.0);

    /* Step 5: period/duty — 末尾密集记录检测纹波.
     * 周期 <2ms 或数据不足时测不出(负载变化快于 INA219 12bit 单次 532us 采样能力),
     * period/duty 置 0 并置 ripple_err 错误码, 上层把错误码上报到 Period DP */
    res->ripple_err = RIPPLE_ERR_NONE;
    /* 纹波分析窗口 = 进入 IH(TP结束) 起, 覆盖可能只出现在保持初段的纹波 */
    int64_t r_win_start = (t_ih_enter > 0) ? t_ih_enter : t_dense_start;
    int r_start = 0;
    while (r_start < rec_cnt && REC_AT(r_start).t_us < r_win_start) {
        r_start++;
    }
    int r_cnt = rec_cnt - r_start;

    if (i_max < NO_PEAK_ABORT_A) {
        /* 无有效峰值: 跳过纹波分析, 避免把基线噪声误判成周期 */
        ESP_LOGW(TAG, "无有效峰值(I_max=%.3fA<%.3fA), 跳过纹波分析, period/duty=0, err=%d",
                 (double)i_max, (double)NO_PEAK_ABORT_A, res->ripple_err);
    } else if (r_cnt >= 8) {
        ESP_LOGI(TAG, "纹波窗口: %.1f~%.1fms r_cnt=%d",
                 (double)((REC_AT(r_start).t_us - t0) / 1000.0),
                 (double)((REC_AT(rec_cnt - 1).t_us - t0) / 1000.0), r_cnt);
        float r_min = 1e6f, r_max = -1e6f;
        for (int j = r_start; j < rec_cnt; j++) {
            if (REC_AT(j).cur > r_max) r_max = REC_AT(j).cur;
            if (REC_AT(j).cur < r_min) r_min = REC_AT(j).cur;
        }
        float r_amp   = r_max - r_min;
        float r_ratio = r_amp / (res->Ih + 0.001f);

        ESP_LOGI(TAG, "纹波检测: amp=%.4fA, ratio=%.1f%% (阈值 %.1f%%)",
                 (double)r_amp, (double)(r_ratio * 100.0),
                 RIPPLE_MIN_AMP_RATIO * 100.0);

        if (r_ratio >= RIPPLE_MIN_AMP_RATIO) {
            float r_mid = (r_max + r_min) * 0.5f;
            int64_t rise_t[32];
            int rise_n = 0;
            bool above = false;

            /* 上升沿检测(迟滞过中点 + 去抖 ≥2ms)
             * 迟滞 = 35% 纹波幅度: 高电平段内部的折线/阶梯波只要谷底高于 15% 幅度线(r_lo)
             * 就不会复位 above, 不产生伪沿; 只有真正的 PWM 低电平(<15%线)才能复位,
             * 从而测出真正的长周期 PWM, 而不是折线波的短周期 */
            float hyst = r_amp * 0.35f;
            float r_hi = r_mid + hyst;
            float r_lo = r_mid - hyst;
            for (int j = r_start; j < rec_cnt && rise_n < 32; j++) {
                if (REC_AT(j).cur > r_hi && !above) {
                    above = true;
                    if (rise_n == 0 || (REC_AT(j).t_us - rise_t[rise_n - 1]) > 2000) {
                        rise_t[rise_n++] = REC_AT(j).t_us;
                    }
                } else if (REC_AT(j).cur < r_lo && above) {
                    above = false;
                }
            }

            if (rise_n >= RIPPLE_MIN_RISE_CNT) {
                int64_t p_sum = 0;
                int p_cnt = 0;
                int64_t p_min = INT64_MAX, p_max = 0;
                for (int k = 1; k < rise_n; k++) {
                    int64_t p = rise_t[k] - rise_t[k - 1];
                    if (p > 100 && p < RIPPLE_MAX_PERIOD_US) {
                        p_sum += p;
                        p_cnt++;
                        if (p < p_min) p_min = p;
                        if (p > p_max) p_max = p;
                    }
                }

                /* 可信度: 周期 ≥2ms(INA219 采样能力下限); 单周期(p_cnt=1)也出值, 多周期要求一致性 */
                bool reliable = (p_cnt >= 1)
                             && (p_sum / p_cnt >= 2000)
                             && (p_cnt < 2 || p_max <= p_min * 2);
                if (reliable) {
                    res->period = p_sum / p_cnt;

                    /* duty: 第一个完整周期内高于中点的点数占比 */
                    int hi = 0, tot = 0;
                    for (int j = r_start; j < rec_cnt; j++) {
                        if (REC_AT(j).t_us >= rise_t[0] && REC_AT(j).t_us < rise_t[1]) {
                            tot++;
                            if (REC_AT(j).cur > r_mid) hi++;
                        }
                    }
                    res->duty = (tot > 0) ? (float)hi / (float)tot : 0.0f;
                    res->ripple_err = RIPPLE_ERR_OK;
                    ESP_LOGI(TAG, "纹波已测量: period=%lldus, duty=%.1f%%, err=%d",
                             (long long)res->period, (double)(res->duty * 100.0),
                             res->ripple_err);
                } else {
                    res->period = 0;
                    res->duty   = 0.0f;
                    res->ripple_err = RIPPLE_ERR_TOO_FAST;
                    ESP_LOGI(TAG, "纹波周期不可信: rise_n=%d p_cnt=%d p_avg=%lldus p_min=%lldus p_max=%lldus"
                                  " (周期<2ms=超带宽或抖动>2×), period/duty=0, err=%d",
                             rise_n, p_cnt,
                             (long long)(p_cnt ? p_sum / p_cnt : 0),
                             (long long)(p_cnt ? p_min : 0),
                             (long long)p_max, res->ripple_err);
                }
            } else {
                res->period = 0;
                res->duty   = 0.0f;
                res->ripple_err = RIPPLE_ERR_EDGES;
                ESP_LOGI(TAG, "上升沿不足(%d<%d), period/duty=0, err=%d",
                         rise_n, RIPPLE_MIN_RISE_CNT, res->ripple_err);
            }
        } else {
            res->period = 0;
            res->duty   = 0.0f;
            res->ripple_err = RIPPLE_ERR_AMP;
            ESP_LOGI(TAG, "纹波不可检测 (幅度<%.0f%% 或频率超带宽), period/duty=0, err=%d",
                     RIPPLE_MIN_AMP_RATIO * 100.0, res->ripple_err);
        }
    } else {
        res->period = 0;
        res->duty   = 0.0f;
        res->ripple_err = RIPPLE_ERR_RECORD;
        ESP_LOGW(TAG, "末尾密集记录不足(%d<8), period/duty=0, err=%d", r_cnt, res->ripple_err);
    }

    // /* 调试曲线: 记录点(带真实时间戳) */
    // int show = (rec_cnt < 40) ? rec_cnt : 40;
    // ESP_LOGI(TAG, "===== 电流变化记录曲线 (前 %d 条) =====", show);
    // for (int j = 0; j < show; j++) {
    //     ESP_LOGI(TAG, "  t=%lld.%03lldms I=%.3fA",
    //              (long long)((REC_AT(j).t_us - t0) / 1000),
    //              (long long)((REC_AT(j).t_us - t0) % 1000),
    //              (double)REC_AT(j).cur);
    // }
    // ESP_LOGI(TAG, "====================================");

    ESP_LOGI(TAG, "结果 | Ip=%.3fA Ih=%.3fA Tp=%.1fms(%lldus) Period=%lldus Duty=%.0f%% ripple_err=%d",
             (double)res->Ip, (double)res->Ih,
             (double)res->Tp / 1000.0, (long long)res->Tp,
             (long long)res->period, (double)(res->duty * 100.0f),
             res->ripple_err);

    free(rec);
    return ESP_OK;
}
