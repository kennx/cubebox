#pragma once

/* 前向声明避免向 main 传染 driver/i2c_master.h。 */
typedef struct i2c_master_bus_t *i2c_master_bus_handle_t;

/* 初始化 QMI8658；失败不致命，显示保持默认方向。须在 hal_display_init 之后调用。 */
void hal_imu_init(i2c_master_bus_handle_t bus);

/* gui_task 上下文调用；每 100ms 读取加速度，方向稳定后同步旋转显示与触摸。
 * 连续 3 次读取失败则保持当前方向并停用轮询，重启后恢复。 */
void hal_imu_service(void);
