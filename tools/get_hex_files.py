import os
import json
import sys
import shutil

CC1352_DIR = os.path.join(os.getcwd(), 'CC1352P7')
RELEASE_FILENAME = "board_release.json"
DEFAULT_IDENT_JSON = 4

def write_json_file(file_name, data):
    with open(file_name, "w") as f:
        json.dump(data, f, indent=DEFAULT_IDENT_JSON)

def find_hex_files(directory):
    for file in os.listdir(directory):
        if file.endswith('.hex'):
            return file
    return None



def main():
    hex_files = []

    # Create a tmp folder to copy the hex files
    os.mkdir("tmp")

    for directory in os.listdir(CC1352_DIR):
        directory_path = os.path.join(CC1352_DIR, directory)
        if os.path.isdir(directory_path):
            hex_file = find_hex_files(directory_path)
            if hex_file:
                hex_files.append(hex_file)
                shutil.copy(os.path.join(directory_path, hex_file), "tmp")
            else:
                hex_file = find_hex_files(os.path.join(directory_path, 'Release'))
                if hex_file:
                    hex_files.append(hex_file)
                    shutil.copy(os.path.join(directory_path, 'Release', hex_file), "tmp")

    

    # Pack the release data into the package
    packet_json = {
        "board_v3": hex_files
    
    }
    write_json_file(RELEASE_FILENAME, packet_json)
    print("Release data packed into %s" % RELEASE_FILENAME)



if __name__ == '__main__':
    try:
        main()
    except Exception as e:
        print("Error: %s" % e)
        sys.exit(1)