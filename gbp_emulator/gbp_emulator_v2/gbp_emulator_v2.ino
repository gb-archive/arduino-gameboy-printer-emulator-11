/*************************************************************************
 *
 * GAMEBOY PRINTER EMULATION PROJECT V2 (Arduino)
 * Copyright (C) 2020 Brian Khuu
 *
 * PURPOSE: To capture gameboy printer images without a gameboy printer
 *          via the arduino platform. (Tested on the arduino nano)
 *          This version is to investigate gameboy behaviour.
 *          This was originally started on 2017-4-6 but updated on 2020-08-16
 * LICENCE:
 *   This file is part of Arduino Gameboy Printer Emulator.
 *
 *   Arduino Gameboy Printer Emulator is free software:
 *   you can redistribute it and/or modify it under the terms of the
 *   GNU General Public License as published by the Free Software Foundation,
 *   either version 3 of the License, or (at your option) any later version.
 *
 *   Arduino Gameboy Printer Emulator is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with Arduino Gameboy Printer Emulator.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#include <stdint.h> // uint8_t
#include <stddef.h> // size_t

#include "gameboy_printer_protocol.h"
#include "gbp_serial_io.h"
#include "gbp_pkt.h"

#define GBP_FEATURE_PACKET_CAPTURE_MODE
#define GBP_FEATURE_PARSE_PACKET_MODE

/* Gameboy Link Cable Mapping to Arduino Pin */
// Note: Serial Clock Pin must be attached to an interrupt pin of the arduino
//  ___________
// |  6  4  2  |
//  \_5__3__1_/   (at cable)
//

#ifdef ESP8266
// Pin Setup for ESP8266 Devices
//                  | Arduino Pin | Gameboy Link Pin  |
#define GBP_VCC_PIN               // Pin 1            : 5.0V (Unused)
#define GBP_SO_PIN       13       // Pin 2            : ESP-pin 7 MOSI (Serial OUTPUT) -> Arduino 13
#define GBP_SI_PIN       12       // Pin 3            : ESP-pin 6 MISO (Serial INPUT)  -> Arduino 12
#define GBP_SD_PIN                // Pin 4            : Serial Data  (Unused)
#define GBP_SC_PIN       14       // Pin 5            : ESP-pin 5 CLK  (Serial Clock)  -> Arduino 14
#define GBP_GND_PIN               // Pin 6            : GND (Attach to GND Pin)
#define LED_STATUS_PIN    2       // Internal LED blink on packet reception
#else
// Pin Setup for Arduinos
//                  | Arduino Pin | Gameboy Link Pin  |
#define GBP_VCC_PIN               // Pin 1            : 5.0V (Unused)
#define GBP_SO_PIN        4       // Pin 2            : Serial OUTPUT
#define GBP_SI_PIN        3       // Pin 3            : Serial INPUT
#define GBP_SD_PIN                // Pin 4            : Serial Data  (Unused)
#define GBP_SC_PIN        2       // Pin 5            : Serial Clock (Interrupt)
#define GBP_GND_PIN               // Pin 6            : GND (Attach to GND Pin)
#define LED_STATUS_PIN   13       // Internal LED blink on packet reception
#endif

/*******************************************************************************
*******************************************************************************/

// Dev Note: Gamboy camera sends data payload of 640 bytes usually
#define GBP_BUFFER_SIZE 10

/* Serial IO */
// This circular buffer contains a stream of raw packets from the gameboy
uint8_t gbp_serialIO_raw_buffer[GBP_BUFFER_SIZE] = {0};

#ifdef GBP_FEATURE_PARSE_PACKET_MODE
/* Packet Buffer */
gbp_pkt_t gbp_pktState = {GBP_REC_NONE, 0};
uint8_t gbp_pktbuff[GBP_PKT_PAYLOAD_BUFF_SIZE_IN_BYTE] = {0};
uint8_t gbp_pktbuffSize = 0;
gbp_pkt_tileAcc_t tileBuff = {0};
#endif

/*******************************************************************************
  Utility Functions
*******************************************************************************/

char *gbpCommand_toStr(int val)
{
  switch (val)
  {
    case GBP_COMMAND_INIT    : return "INIT";
    case GBP_COMMAND_PRINT   : return "PRNT";
    case GBP_COMMAND_DATA    : return "DATA";
    case GBP_COMMAND_BREAK   : return "BREK";
    case GBP_COMMAND_INQUIRY : return "INQY";
    default: return "?";
  }
}

/*******************************************************************************
  Interrupt Service Routine
*******************************************************************************/

#ifdef ESP8266
void ICACHE_RAM_ATTR serialClock_ISR(void)
#else
void serialClock_ISR(void)
#endif
{
  // Serial Clock (1 = Rising Edge) (0 = Falling Edge); Master Output Slave Input (This device is slave)
#ifdef GBP_FEATURE_USING_RISING_CLOCK_ONLY_ISR
  const bool txBit = gpb_serial_io_OnRising_ISR(digitalRead(GBP_SO_PIN));
#else
  const bool txBit = gpb_serial_io_OnChange_ISR(digitalRead(GBP_SC_PIN), digitalRead(GBP_SO_PIN));
#endif
  digitalWrite(GBP_SI_PIN, txBit ? HIGH : LOW);
}


/*******************************************************************************
  Main Setup and Loop
*******************************************************************************/

void setup(void)
{
  // Config Serial
  // Has to be fast or it will not transfer the image fast enough to the computer
  Serial.begin(115200);

  /* Pins from gameboy link cable */
  pinMode(GBP_SC_PIN, INPUT);
  pinMode(GBP_SO_PIN, INPUT);
  pinMode(GBP_SI_PIN, OUTPUT);

  /* Default link serial out pin state */
  digitalWrite(GBP_SI_PIN, LOW);

  /* LED Indicator */
  pinMode(LED_STATUS_PIN, OUTPUT);
  digitalWrite(LED_STATUS_PIN, LOW);

  /* Setup */
  gpb_serial_io_init(sizeof(gbp_serialIO_raw_buffer), gbp_serialIO_raw_buffer);

  /* Attach ISR */
#ifdef GBP_FEATURE_USING_RISING_CLOCK_ONLY_ISR
  attachInterrupt( digitalPinToInterrupt(GBP_SC_PIN), serialClock_ISR, RISING);  // attach interrupt handler
#else
  attachInterrupt( digitalPinToInterrupt(GBP_SC_PIN), serialClock_ISR, CHANGE);  // attach interrupt handler
#endif

  /* Packet Parser */
#ifdef GBP_FEATURE_PARSE_PACKET_MODE
  gbp_pkt_init(&gbp_pktState);
#endif

  /* Welcome Message */
  Serial.print("# GAMEBOY PRINTER Emulator V2 : Copyright (C) 2020 Brian Khuu\n");
  Serial.print("# JS Decoder: https://mofosyne.github.io/arduino-gameboy-printer-emulator/gbp_decoder/jsdecoderV2/gameboy_printer_js_decoder.html\n");
  Serial.print("# --- GNU GENERAL PUBLIC LICENSE Version 3, 29 June 2007 ---\n");
  Serial.print("# This program comes with ABSOLUTELY NO WARRANTY;\n");
  Serial.print("# This is free software, and you are welcome to redistribute it\n");
  Serial.print("# under certain conditions. Refer to LICENSE file for detail.\n");
  Serial.print("# --- \n");

} // setup()

void loop()
{
  static uint16_t sioWaterline = 0;
  static bool packet_capture_mode_enabled = false;
  static bool packet_capture_mode_cstyle = false;

  if (packet_capture_mode_enabled)
  {
    gbp_packet_capture_loop(packet_capture_mode_cstyle);
  }
  else
  {
    gbp_parse_packet_loop();
  }

  // Trigger Timeout and reset the printer if byte stopped being received.
  static uint32_t last_millis = 0;
  uint32_t curr_millis = millis();
  if (curr_millis > last_millis)
  {
    uint32_t elapsed_ms = curr_millis - last_millis;
    if (gbp_serial_io_timeout_handler(elapsed_ms))
    {
      if (packet_capture_mode_cstyle)
      {
        Serial.println("\n\n/* Timed Out */\n\n");
      }
      else
      {
        Serial.println("\n\n# Timed Out\n\n");
      }
      digitalWrite(LED_STATUS_PIN, LOW);
    }
  }
  last_millis = curr_millis;

  // Diagnostics Console
  while (Serial.available() > 0)
  {
    switch (Serial.read())
    {
      case 'p':
        Serial.print("# (P)arse Mode\n");
        Serial.print("# GAMEBOY PRINTER Emulator V2 (Brian Khuu 2020)\n");
        Serial.print("# To Decode This: https://mofosyne.github.io/arduino-gameboy-printer-emulator/jsdecoder/gameboy_printer_js_decoder.html\n");
        packet_capture_mode_enabled = false;
        packet_capture_mode_cstyle = false;
        break;

      case 'r':
        Serial.print("// (R)aw Packet Capture Mode\n");
        Serial.print("// GAMEBOY PRINTER Packet Capture V2 (Brian Khuu 2020)\n");
        Serial.print("// Note: Each byte is from each GBP packet is from the gameboy\n");
        Serial.print("//       except for the last two bytes which is from the printer\n");
        packet_capture_mode_enabled = true;
        packet_capture_mode_cstyle = false;
        break;

      case 'c':
        Serial.print("/* (C)Style Packet Capture Mode */\n");
        Serial.print("// GAMEBOY PRINTER Packet Capture V2 (Brian Khuu 2020)\n");
        Serial.print("// Note: Each byte is from each GBP packet is from the gameboy\n");
        Serial.print("//       except for the last two bytes which is from the printer\n");
        packet_capture_mode_enabled = true;
        packet_capture_mode_cstyle = true;
        break;

      case '?':
        Serial.print("p=packetMode, r=rawMode, c=cStyleCapture, d=debug, ?=help\n");
        break;

      case 'd':
        Serial.print("waterline: ");
        Serial.print(gbp_serial_io_dataBuff_waterline(false));
        Serial.print("B out of ");
        Serial.print(gbp_serial_io_dataBuff_max());
        Serial.print("B\n");
        break;
    }
  };
} // loop()

/******************************************************************************/

void gbp_parse_packet_loop()
{
#ifdef GBP_FEATURE_PARSE_PACKET_MODE
  const char nibbleToCharLUT[] = "0123456789ABCDEF";
  for (int i = 0 ; i < gbp_serial_io_dataBuff_getByteCount() ; i++)
  {
    if (gbp_pkt_processByte(&gbp_pktState, (const uint8_t) gbp_serial_io_dataBuff_getByte(), gbp_pktbuff, &gbp_pktbuffSize, sizeof(gbp_pktbuff)))
    {
      if (gbp_pktState.received == GBP_REC_GOT_PACKET)
      {
          digitalWrite(LED_STATUS_PIN, HIGH);
          Serial.print((char)'!');
          Serial.print((char)'{');
          Serial.print("\"command\":\"");
          Serial.print(gbpCommand_toStr(gbp_pktState.command));
          Serial.print("\"");
          if (gbp_pktState.command == GBP_COMMAND_INQUIRY)
          {
            // !{"command":"INQY","status":{"lowbatt":0,"jam":0,"err":0,"pkterr":0,"unproc":1,"full":0,"bsy":0,"chk_err":0}}
            Serial.print(", \"status\":{");
            Serial.print("\"LowBat\":");
            Serial.print(gpb_status_bit_getbit_low_battery(gbp_pktState.status)      ? '1' : '0');
            Serial.print(",\"ER2\":");
            Serial.print(gpb_status_bit_getbit_other_error(gbp_pktState.status)      ? '1' : '0');
            Serial.print(",\"ER1\":");
            Serial.print(gpb_status_bit_getbit_paper_jam(gbp_pktState.status)        ? '1' : '0');
            Serial.print(",\"ER0\":");
            Serial.print(gpb_status_bit_getbit_packet_error(gbp_pktState.status)     ? '1' : '0');
            Serial.print(",\"Untran\":");
            Serial.print(gpb_status_bit_getbit_unprocessed_data(gbp_pktState.status) ? '1' : '0');
            Serial.print(",\"Full\":");
            Serial.print(gpb_status_bit_getbit_print_buffer_full(gbp_pktState.status)? '1' : '0');
            Serial.print(",\"Busy\":");
            Serial.print(gpb_status_bit_getbit_printer_busy(gbp_pktState.status)     ? '1' : '0');
            Serial.print(",\"Sum\":");
            Serial.print(gpb_status_bit_getbit_checksum_error(gbp_pktState.status)   ? '1' : '0');
            Serial.print((char)'}');
          }
          if (gbp_pktState.command == GBP_COMMAND_PRINT)
          {
            //!{"command":"PRNT","sheets":1,"margin_upper":1,"margin_lower":3,"pallet":228,"density":64 }
            Serial.print(", \"sheets\":");
            Serial.print(gbp_pkt_printInstruction_num_of_sheets(gbp_pktbuff));
            Serial.print(", \"margin_upper\":");
            Serial.print(gbp_pkt_printInstruction_num_of_linefeed_before_print(gbp_pktbuff));
            Serial.print(", \"margin_lower\":");
            Serial.print(gbp_pkt_printInstruction_num_of_linefeed_after_print(gbp_pktbuff));
            Serial.print(", \"pallet\":");
            Serial.print(gbp_pkt_printInstruction_palette_value(gbp_pktbuff));
            Serial.print(", \"density\":");
            Serial.print(gbp_pkt_printInstruction_print_density(gbp_pktbuff));
          }
          if (gbp_pktState.command == GBP_COMMAND_DATA)
          {
            //!{"command":"DATA", "compressed":0, "more":0}
            Serial.print(", \"compressed\":");
            Serial.print(gbp_pktState.compression);
            Serial.print(", \"more\":");
            Serial.print((gbp_pktState.dataLength != 0)?'1':'0');
          }
          Serial.print((char)'}');
          Serial.print("\r\n");
      }
      else
      {
#if 1
        // Required for more complex games with compression support
        while (gbp_pkt_decompressor(&gbp_pktState, gbp_pktbuff, gbp_pktbuffSize, &tileBuff))
        {
          if (gbp_pkt_tileAccu_tileReadyCheck(&tileBuff))
          {
            // Got Tile
            for (int i = 0 ; i < GBP_TILE_SIZE_IN_BYTE ; i++)
            {
              const uint8_t data_8bit = tileBuff.tile[i];
              Serial.print((char)nibbleToCharLUT[(data_8bit>>4)&0xF]);
              Serial.print((char)nibbleToCharLUT[(data_8bit>>0)&0xF]);
              Serial.print((char)' ');
            }
            Serial.print((char)'\r');
            Serial.print((char)'\n');
          }
        }
#else
        // Simplified support for gameboy camera only application
        // Dev Note: Good for checking if everything above decompressor is working
        if (gbp_pktbuffSize == GBP_TILE_SIZE_IN_BYTE)
        {
          // Got Tile
          for (int i = 0 ; i < GBP_TILE_SIZE_IN_BYTE ; i++)
          {
            const uint8_t data_8bit = gbp_pktbuff[i];
            Serial.print((char)nibbleToCharLUT[(data_8bit>>4)&0xF]);
            Serial.print((char)nibbleToCharLUT[(data_8bit>>0)&0xF]);
            Serial.print((char)' ');
          }
          Serial.print((char)'\r');
          Serial.print((char)'\n');
        }
#endif
      }
    }
  }
#endif
}

void gbp_packet_capture_loop(bool cStyle)
{
#ifdef GBP_FEATURE_PACKET_CAPTURE_MODE
  /* tiles received */
  static uint32_t byteTotal = 0;
  static uint32_t pktTotalCount = 0;
  static uint32_t pktByteIndex = 0;
  static uint16_t pktDataLength = 0;
  const size_t dataBuffCount = gbp_serial_io_dataBuff_getByteCount();
  if (
      ((pktByteIndex != 0)&&(dataBuffCount>0))||
      ((pktByteIndex == 0)&&(dataBuffCount>=6))
      )
  {
    const char nibbleToCharLUT[] = "0123456789ABCDEF";
    uint8_t data_8bit = 0;
    for (int i = 0 ; i < dataBuffCount ; i++)
    { // Display the data payload encoded in hex
      // Start of a new packet
      if (pktByteIndex == 0)
      {
        pktDataLength = gbp_serial_io_dataBuff_getByte_Peek(4);
        pktDataLength |= (gbp_serial_io_dataBuff_getByte_Peek(5)<<8)&0xFF00;
        Serial.print("// ");
        Serial.print(pktTotalCount);
        Serial.print(" : ");
        Serial.print(gbpCommand_toStr(gbp_serial_io_dataBuff_getByte_Peek(2)));
        Serial.print("\n");
        digitalWrite(LED_STATUS_PIN, HIGH);
      }
      // Print Hex Byte
      data_8bit = gbp_serial_io_dataBuff_getByte();
      if (cStyle)
      {
        Serial.print((char)'0');
        Serial.print((char)'x');
      }
      Serial.print((char)nibbleToCharLUT[(data_8bit>>4)&0xF]);
      Serial.print((char)nibbleToCharLUT[(data_8bit>>0)&0xF]);
      Serial.print((char)(cStyle?',':' '));
      // Splitting packets for convenience
      if ((pktByteIndex>5)&&(pktByteIndex>=(9+pktDataLength)))
      {
        digitalWrite(LED_STATUS_PIN, LOW);
        Serial.print("\n");
        pktByteIndex = 0;
        pktTotalCount++;
      }
      else
      {
        if (cStyle)
        {
          Serial.print(((pktByteIndex+1)%16 == 0)?'\n':' '); ///< Insert Newline Periodically
        }
        pktByteIndex++; // Byte hex split counter
        byteTotal++; // Byte total counter
      }
    }
  }
#endif
}
