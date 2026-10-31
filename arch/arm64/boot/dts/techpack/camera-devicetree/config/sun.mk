dtbo-$(CONFIG_ARCH_SUN)   := sun-camera.dtbo
dtbo-$(CONFIG_ARCH_SUN)   += sun-camera-sensor-mtp.dtbo \
				sun-camera-sensor-rumi.dtbo \
				sun-camera-sensor-cdp.dtbo  \
				sun-camera-sensor-hdk.dtbo  \
				sun-camera-sensor-qrd.dtbo  \
				haotian-sm8750-camera-sensor.dtbo \
				dada-sm8750-camera-sensor.dtbo \
				miro-sm8750-camera-sensor.dtbo \
				xuanyuan-sm8750-camera-sensor.dtbo \
				bixi-sm8750-camera-sensor.dtbo \
				nirvana-sm8750-camera-sensor.dtbo \
				piano-sm8750-camera-sensor.dtbo


dtbo-$(CONFIG_ARCH_TUNA)  += tuna-camera.dtbo

dtbo-$(CONFIG_ARCH_TUNA)  += tuna-camera-sensor-mtp.dtbo \
				tuna-camera-sensor-cdp.dtbo \
				tuna-camera-sensor-qrd.dtbo \
				onyx-sm8735-camera-sensor.dtbo \
				luming-sm8735-camera-sensor.dtbo \
				yupei-sm8735-camera-sensor.dtbo

dtbo-$(CONFIG_ARCH_KERA)  += kera-camera.dtbo
dtbo-$(CONFIG_ARCH_KERA)  += kera-camera-sensor-mtp.dtbo \
				kera-camera-sensor-cdp.dtbo \
				kera-camera-sensor-qrd.dtbo \
				kera-camera-sensor-rcm.dtbo
