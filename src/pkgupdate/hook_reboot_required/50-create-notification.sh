#!/bin/sh
# Do not try to create notification for run on non-root system
[ "$ROOT_DIR" = "/" ] || exit 0

create_notification -s restart \
	"Systém byl aktualizován, ale některé změny se projeví až po restartu. Prosím restartujte své zařízení." \
	"The system was updated, but some changes will take effect only after reboot. Please reboot the device." \
		|| echo "Create notification failed"
