ccflags-y += -Idrivers/media/platform/msm/camera_v2
ccflags-y += -Idrivers/media/platform/msm/camera_v2/common
ccflags-y += -Idrivers/media/platform/msm/camera_v2/msm_vb2
ccflags-y += -Idrivers/media/platform/msm/camera_v2/camera
ccflags-y += -Idrivers/media/platform/msm/camera_v2/sensor/io
ccflags-y += -Idrivers/media/platform/msm/camera_v2/sensor/cci
obj-$(CONFIG_MSMB_CAMERA) += cci/ io/ csiphy/ csid/ actuator/ eeprom/ ois/ ir_led/ ir_cut/

# AW36413 flash directory have too many changes.
# I am too lazy to add over 9000 #ifdef.
ifdef CONFIG_MACH_MEIZU_M1721
obj-$(CONFIG_MSMB_CAMERA) += flash_aw36413/
else
obj-$(CONFIG_MSMB_CAMERA) += flash/
endif

obj-$(CONFIG_MSM_CAMERA_SENSOR) += msm_sensor_init.o msm_sensor_driver.o msm_sensor.o
