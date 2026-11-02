/*
 * LXR-02 Emu (unofficial) - stand-in for the customised usbd_cdc.h
 * (Middlewares/JS/.../Class/CDC/Inc). The USB device stack is not built; only
 * the USB-MIDI queues that Soundengine/USB/usb_manager.c uses are declared
 * here and defined in emu/hw/usb_emu.c.
 * SPDX-License-Identifier: GPL-3.0-only
 */
#ifndef EMU_USBD_CDC_H
#define EMU_USBD_CDC_H
#include <stdint.h>
#include "Soundengine/MIDI/MidiMessages.h"

#define USB_MIDI_INPUT_BUFFER_SIZE 	64
#define USB_MIDI_INPUT_BUFFER_MASK	0x3F
#define TOTAL_MIDI_BUF_SIZE		0x40
#define NUM_SUB_BUFFERS			0x2

extern MidiMsg usb_MidiMessages[USB_MIDI_INPUT_BUFFER_SIZE];
extern uint8_t usb_MidiMessagesRead;
extern uint8_t usb_MidiMessagesWrite;

extern uint8_t  usb_MidiInBuff [TOTAL_MIDI_BUF_SIZE * NUM_SUB_BUFFERS];
extern uint8_t* usb_MidiInWrPtr;
extern uint8_t* usb_MidiInRdPtr;

/* Stand-ins for the USB device stack, which is not built here: emu/hw/usb_emu.c
 * provides the transport instead. They exist so that
 * Soundengine/USB/usb_manager.c compiles unchanged, which keeps its MIDI queue
 * handling - the subject of issue #12 - under test. usb_init() and its
 * relatives are never called by the emulator; platform.c does that job. */
typedef enum { USBD_OK = 0, USBD_BUSY, USBD_FAIL } USBD_StatusTypeDef;
typedef enum { USBD_STATE_DEFAULT = 1, USBD_STATE_ADDRESSED, USBD_STATE_CONFIGURED,
               USBD_STATE_SUSPENDED } USBD_StateTypeDef;

typedef struct { uint32_t TxState; uint32_t RxState; } USBD_CDC_HandleTypeDef;
typedef struct { uint32_t dev_state; void *pClassData; } USBD_HandleTypeDef;

extern USBD_HandleTypeDef hUsbDeviceFS;
extern USBD_HandleTypeDef USB_OTG_dev;
extern int FS_Desc, USBD_CDC, USBD_Interface_fops_FS, MIDI_cb;
#define DEVICE_FS 0

USBD_StatusTypeDef USBD_Init(USBD_HandleTypeDef *h, void *desc, uint8_t id);
USBD_StatusTypeDef USBD_DeInit(USBD_HandleTypeDef *h);
USBD_StatusTypeDef USBD_RegisterClass(USBD_HandleTypeDef *h, void *cls);
USBD_StatusTypeDef USBD_CDC_RegisterInterface(USBD_HandleTypeDef *h, void *fops);
USBD_StatusTypeDef USBD_Start(USBD_HandleTypeDef *h);

#endif
