params_egb = {
    "PID": [
        {
            "index": 0,
            "name": "PID Kp",
            "key": "kp",
            "limits": (0.0, 20.0)
        },
        {"index": 1, "name": "PID Kd", "key": "kd", "limits": (0.0, 0.2)},
        {"index": 2, "name": "PID Ki", "key": "ki", "limits": (0.0, 0.5)},
        {"index": 3, "name": "Resistencia objetivo", "key": "r_target", "limits": (20, 2000)},
        {"index": 4, "name": "Tiempo de variación", "key": "t_target", "limits": (0, 60000)},
        {"index": 5, "name": "Límite termino integral", "key": "int_lim", "limits": (0.0, 25.0)},
        {"index": 6, "name": "Límite termino derivativa", "key": "der_lim", "limits": (0.0, 50.0)},
    ],
    "Informacion": [
        {"index": 0, "name": "Estado", "key": "status"},
        {"index": 1, "name": "Voltaje", "key": "voltage"},
        {"index": 2, "name": "Corriente", "key": "current"},
        {"index": 3, "name": "Temperatura", "key": "temp"},
        # {"index": 4, "name": "Memoria SD", "key": "sd_card"},
    ],
    "Protecciones": [
        {"index": 0, "name": "Temperatura máxima", "key": "max_temp", "limits": (50.0, 130.0)},
        {"index": 1, "name": "Corriente máxima", "key": "max_current", "limits": (0.5, 0.85)},
        {"index": 2, "name": "Voltaje máximo", "key": "max_voltage", "limits": (3.0, 12.0)},
    ]
}

set_keys = {
    "PID Kp": "kp",
    "PID Kd": "kd",
    "PID Ki": "ki",
    "Resistencia objetivo": "r_target",
    "Tiempo de variación": "t_target",
    "Límite termino integral": "int_lim",
    "Límite termino derivativa": "der_lim",
    "Temperatura máxima": "max_temp",
    "Corriente máxima": "max_current",
    "Voltaje máximo": "max_voltage"
}

get_keys = {
    **set_keys,
    "Estado": "status",
    "Protecciones": "protec",
    "Voltaje": "voltage",
    "Corriente": "current",
    "Temperatura": "temp",
    # "Memoria SD": "sd_card"
}

data_keys = ["Voltage", "Current", "PWM Value", "Error", "Integral", "Derivative", "R_Target", "Temperature"]