#!/usr/bin/env python3

"""
Receives file uploads from HTTP POST requests and saves each file as:
  - ./${randstr}      : contents of received file
  - ./${randstr}.path : path of received file (if exists in the request)
"""

import os
from sys import argv
import http.server as server
import random, string

# Defaults
RAND_FILE_LEN = 6
DEFAULT_PORT = 8080


class HTTPRequestHandler(server.BaseHTTPRequestHandler):
    """Extend SimpleHTTPRequestHandler to handle PUT requests"""
    def do_POST(self):
        """Save a file from an HTTP POST request"""
        # Get the file's length
        if 'Content-Length' not in self.headers: 
            printf("[!] No Content-Length header")    
            self.send_response_end_headers(411)  # 411 Length Required
            return
        file_length = int(self.headers['Content-Length'])
        
        # Try to get the file's path if exists in the request
        filepath = ""
        if 'Content-Disposition' in self.headers:
            if 'filename' in self.headers['Content-Disposition']:
                try:
                    # Extract file path from 'filename="/some/file/path"; otherfield="othervalue"...'
                    after_filename = self.headers['Content-Disposition'].split("filename=")[-1]
                    filepath = after_filename.split(";")[0].strip("\"") 
                except Exception: # very lazy try/except clause
                    filepath = ""

        
        # Generate a random name to save the file as
        save_as = rand_filename(RAND_FILE_LEN)
        if filepath:
            print(f"[+] Saving file (path={filepath}, len={file_length}) to '{save_as}' and '{save_as}.path'")
        else:
            print(f"[+] Received unnamed file (len={file_length}), saving to '{save_as}'")
        
        # Write file to save_as
        try:
            with open(save_as, 'wb') as output_file:
                output_file.write(self.rfile.read(file_length))
        except IOError as e:
            print(f"[!] Failed to write file to '{save_as}'' with '{repr(e)}'")
            self.send_response_end_headers(500)
            return
            
        # Write received file path to f'{save_as}.path'
        if filepath:
            try:
                with open(save_as + ".path", 'w') as name_file:
                    name_file.write(filepath)
            except IOError as e:
                print(f"[!] Failed to write filepath ('{filepath}'') to '{save_as}.path' with '{repr(e)}'")

        self.send_response_end_headers(200)


    def send_response_end_headers(self, code):
        self.send_response(code)
        print()  # create a newline between requests for prettier output
        self.end_headers()     


# Get a random, non-existent filename of @length chars
def rand_filename(length):
    filename = rand_word(length)
    while os.path.exists(filename):
        filename = rand_word(length)
    return filename

# Generate a random word of @length chars
def rand_word(length):
   letters = string.ascii_lowercase
   return ''.join(random.choice(letters) for i in range(length))


def main():
    if len(argv) > 1:
        port = int(argv[1])
    else:
        port = DEFAULT_PORT

    server_address = ("0.0.0.0", port)
    httpd = server.HTTPServer(server_address, HTTPRequestHandler)
    print("Waiting for file uploads at {}\n".format(port))
    httpd.serve_forever()


if __name__ == '__main__':
    main()
