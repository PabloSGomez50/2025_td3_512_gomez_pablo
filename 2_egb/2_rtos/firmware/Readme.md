# EGA - 2025

### Cambios importantes
Cambios en system_config:
- Al utilizar el GPIO PWM_ENABLE se elimina la var global pid_enable
- La validacion de memoria SD se realiza en la tarea de LCD y datalogger. Se elimina sd_mounted.

### TODO
- Implementar la habilitacion del PID con btn_stop
