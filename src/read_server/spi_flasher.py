import argparse
import base64
import hashlib
import math
import os
import random
import time

import serial


DATA_CHUNK_SIZE = 2048
DEFAULT_BAUD_RATE = 9600

COMMAND_CHARS = {
    'SET_BAUD': b'!',
    'SET_ERASE': b'@',
    'SET_WRITE': b'#',
    'SET_FILE_SIZE': b'$',
    'SEND_FLASH_DATA': b'%',
    'DO_ERASE': b'^',
    'DO_FLASH': b'&',
    'DO_RESET': b'*'
}

MESSAGE_TYPES = {
    '#': 'INFO',
    '!': 'ERROR',
    '@': 'MD5'
}

# ------------
def initialize_device(port, baud_rate):
    """
    Change the ESP*'s baud rate
    """

    print('Initiating connection...')

    try:
        with serial.Serial(port, DEFAULT_BAUD_RATE, timeout=2) as esp_connection:
            write_command(esp_connection, 'SET_BAUD', baud_rate)
            handle_serial_message(esp_connection)

    except serial.SerialException:
        print(f'ERROR: Could not connect to device on {port}. Check your connections.')
        return False

    return True

# ----
def do_flash(rom_file, port, baud_rate, do_erase, do_write):
    """
    The bulk of the script logic; sends all flashing-related commands
    """

    print('Reading file...')

    with open(rom_file, 'rb') as rfile:
        rom_data = rfile.read()

    rom_file_len = len(rom_data)
    with serial.Serial(port, baud_rate, timeout=.25) as esp_connection:
        print('Setting things up...')

        write_command(esp_connection, 'SET_ERASE', b'1' if do_erase else b'0')
        handle_serial_message(esp_connection)

        write_command(esp_connection, 'SET_WRITE', b'1' if do_write else b'0')
        handle_serial_message(esp_connection)

        write_command(esp_connection, 'SET_FILE_SIZE', rom_file_len)
        handle_serial_message(esp_connection)

    # Increase the timeout now that we're sending non-trivial data
    with serial.Serial(port, baud_rate, timeout=5) as esp_connection:
        if do_erase:
            write_command(esp_connection, 'DO_ERASE')

            # This also outputs the status of the erase
            while handle_serial_message(esp_connection) != 'Chip erased':
                pass

        # Send data
        if do_write:
            print('\nWrite in progress...')

            chunks_to_complete = math.ceil(rom_file_len / DATA_CHUNK_SIZE)
            log_interval = int(round(chunks_to_complete / 100, 0))

            for rom_file_pos in range(0, rom_file_len, DATA_CHUNK_SIZE):
                write_end_index = min(rom_file_pos + DATA_CHUNK_SIZE, rom_file_len)
                data_to_write = rom_data[rom_file_pos: write_end_index]
                data_hash = hashlib.md5(data_to_write).hexdigest()

                # Loop until data matches up
                while True:
                    write_command(esp_connection, 'SEND_FLASH_DATA', data_to_write)

                    recv_hash = handle_serial_message(esp_connection, mute_info=True, mandatory=True)
                    if recv_hash == data_hash:
                        write_command(esp_connection, 'DO_FLASH')

                        # Wait for write to complete
                        while True:
                            if handle_serial_message(esp_connection, mute_info=True) == 'W_OK':
                                break

                        break

                    else:
                        print('Hash mismatch, retrying...')

                if rom_file_pos > 0 and rom_file_pos % log_interval == 0:
                    print(f'{rom_file_pos}/{rom_file_len} ({round(((rom_file_pos / rom_file_len) * 100)):d}%) written')
            
            print(f'{rom_file_len}/{rom_file_len} (100%) written')  # Ensure satisfactory ending
            print('\nWrite complete!')

            write_command(esp_connection, 'DO_RESET')

    return True

# ------------
# Helper methods

def handle_serial_message(serial_connection, mute_info=False, mandatory=False):
    """
    Echoes INFO messages if mute_info is not True
    Raises exception on errors and unknown message types
    Returns message data for MD5 and INFO
    """

    data = serial_connection.readline()
    output = data.decode('ascii').strip()

    if len(output) == 0:
        if mandatory:
            raise Exception('Did not receive expected serial message')
        return ''

    message_type_char = output[0]
    message_data = output[1:]
    message_type = MESSAGE_TYPES.get(message_type_char, None)

    if message_type is None:
        raise Exception(f'Unknown message type "{message_type_char}" with data "{message_data}"')

    elif message_type == 'ERROR':
        raise Exception(message_data.replace('ERROR: ', ''))

    elif message_type == 'INFO':
        if not mute_info:
            print(message_data)

    elif message_type == 'MD5':
        pass  # just return data

    return message_data

# ----
def write_command(serial_connection, command, data=None):
    """
    Handles conversion of commands to friendly names, data to expected form,
    and the overall message to the correct format.
    """

    if type(data) is int:
        data = data.to_bytes(4, 'little')  # unsigned 32-bit int
    elif type(data) is not bytes:
        data = str(data).encode('ascii')

    data = b'' if data is None else base64.b64encode(data)
    serial_connection.write(COMMAND_CHARS[command] + data + b'\n')

# ------------
def main():
    """
    Handle arguments and run script
    """

    parser = argparse.ArgumentParser(description='Basic ROM Flasher')

    parser.add_argument('-file', nargs='?', required=True, help='The file to flash to the ROM')
    parser.add_argument('-port', nargs='?', required=True, help='The COM port to connect to')
    parser.add_argument('-baud', nargs='?', type=int, required=True, help='Baud rate to communicate at; try a high value like: 921600, 700000, 576000, 250000, 115200')
    parser.add_argument('--erase', action='store_true', help='Erase the chip')
    parser.add_argument('--write', action='store_true', help='Write to the chip')

    args = parser.parse_args()

    if not os.path.exists(args.file):
        print('Provided file does not exist\nFlash failed')
        return

    if initialize_device(args.port, args.baud) is False:
        print('Flash failed')
        return

    flash_status_code = do_flash(args.file, args.port, args.baud, args.erase, args.write)
    if flash_status_code is False:
        print('Flash failed')

# ----
if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        print('Exiting...')
    finally:
        print('')
