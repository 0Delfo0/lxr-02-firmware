/*
 * LXR-02 Emu (unofficial) - USB-MIDI queues normally defined by the USB
 * device class (usbd_cdc.c). Nothing is connected to them: MIDI from the
 * browser goes through the DIN port (USART3) instead, and what the firmware
 * sends over USB is dropped, since it duplicates the DIN output.
 *
 * Copyright (C) 2026 LXR-02 Emu contributors
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "usbd_cdc_if.h"

MidiMsg usb_MidiMessages[USB_MIDI_INPUT_BUFFER_SIZE];
uint8_t usb_MidiMessagesRead = 0;
uint8_t usb_MidiMessagesWrite = 0;

uint8_t  usb_MidiInBuff [TOTAL_MIDI_BUF_SIZE * NUM_SUB_BUFFERS];
uint8_t* usb_MidiInWrPtr = usb_MidiInBuff;
uint8_t* usb_MidiInRdPtr = usb_MidiInBuff;

uint8_t CDC_Transmit_FS(uint8_t *Buf, uint16_t Len)
{
	(void)Buf; (void)Len;
	return 0;
}

/* The USB device stack stand-ins declared in emu/shim/usbd_cdc.h. The emulator
 * is always "configured" with a class attached, so usb_isConnected() and the
 * TxState check in usb_manager.c behave as they do on a connected device. */
USBD_CDC_HandleTypeDef emu_cdc_handle;
USBD_HandleTypeDef hUsbDeviceFS = { USBD_STATE_CONFIGURED, &emu_cdc_handle };
USBD_HandleTypeDef USB_OTG_dev  = { USBD_STATE_CONFIGURED, &emu_cdc_handle };
int FS_Desc, USBD_CDC, USBD_Interface_fops_FS, MIDI_cb;

USBD_StatusTypeDef USBD_Init(USBD_HandleTypeDef *h, void *desc, uint8_t id)
{ (void)h; (void)desc; (void)id; return USBD_OK; }
USBD_StatusTypeDef USBD_DeInit(USBD_HandleTypeDef *h) { (void)h; return USBD_OK; }
USBD_StatusTypeDef USBD_RegisterClass(USBD_HandleTypeDef *h, void *cls)
{ (void)h; (void)cls; return USBD_OK; }
USBD_StatusTypeDef USBD_CDC_RegisterInterface(USBD_HandleTypeDef *h, void *fops)
{ (void)h; (void)fops; return USBD_OK; }
USBD_StatusTypeDef USBD_Start(USBD_HandleTypeDef *h) { (void)h; return USBD_OK; }
