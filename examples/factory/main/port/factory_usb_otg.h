#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum factory_usb_role {
    FACTORY_USB_ROLE_HOST = 0,
    FACTORY_USB_ROLE_DEVICE,
} factory_usb_role_t;

typedef struct factory_usb_otg_state {
    factory_usb_role_t role;
    bool stack_running;
    bool stack_ready;
    bool peer_detected;
    bool external_vbus;
    bool otg_powered;
    bool boost_fault;
    uint8_t device_count;
    uint8_t device_address;
    uint8_t device_class;
    uint8_t device_subclass;
    uint8_t device_protocol;
    uint8_t device_speed;
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t vbus_voltage_mv;
    char error[96];
} factory_usb_otg_state_t;

bool factory_usb_otg_start(factory_usb_role_t role);
void factory_usb_otg_stop(void);
void factory_usb_otg_refresh(void);
void factory_usb_otg_get_state(factory_usb_otg_state_t *state);

#ifdef __cplusplus
}  // extern "C"
#endif
