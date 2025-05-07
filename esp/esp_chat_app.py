import tkinter as tk
from tkinter import ttk, filedialog, scrolledtext, messagebox
import serial
import serial.tools.list_ports
import threading
import time
import os
from PIL import Image, ImageTk
import io
import sys

class ESPChatApp:
    def __init__(self, root):
        self.root = root
        self.root.title("ESP8266 Chat & Image Transfer")
        self.root.geometry("800x600")
        self.root.minsize(800, 600)
        
        self.serial_port = None
        self.connected = False
        self.serial_thread = None
        self.stop_thread = False
        
        self.setup_ui()
        self.update_port_list()
        
    def setup_ui(self):
        # Main frame
        main_frame = ttk.Frame(self.root, padding=10)
        main_frame.pack(fill=tk.BOTH, expand=True)
        
        # Top frame for connection controls
        top_frame = ttk.Frame(main_frame)
        top_frame.pack(fill=tk.X, pady=(0, 10))
        
        # Port selection
        ttk.Label(top_frame, text="Port:").pack(side=tk.LEFT, padx=(0, 5))
        self.port_combobox = ttk.Combobox(top_frame, width=15, state="readonly")
        self.port_combobox.pack(side=tk.LEFT, padx=(0, 10))
        
        # Refresh button
        ttk.Button(top_frame, text="Refresh", command=self.update_port_list).pack(side=tk.LEFT, padx=(0, 10))
        
        # Connect/Disconnect button
        self.connect_button = ttk.Button(top_frame, text="Connect", command=self.toggle_connection)
        self.connect_button.pack(side=tk.LEFT)
        
        # Status label
        self.status_var = tk.StringVar(value="Not Connected")
        ttk.Label(top_frame, textvariable=self.status_var).pack(side=tk.RIGHT)
        
        # Split frame for chat and image
        split_frame = ttk.Frame(main_frame)
        split_frame.pack(fill=tk.BOTH, expand=True)
        
        # Chat frame (left side)
        chat_frame = ttk.LabelFrame(split_frame, text="Chat")
        chat_frame.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=(0, 5))
        
        # Chat display
        self.chat_display = scrolledtext.ScrolledText(chat_frame, wrap=tk.WORD, state=tk.DISABLED)
        self.chat_display.pack(fill=tk.BOTH, expand=True, pady=(0, 10))
        
        # Message entry
        message_frame = ttk.Frame(chat_frame)
        message_frame.pack(fill=tk.X)
        
        self.message_entry = ttk.Entry(message_frame)
        self.message_entry.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(0, 5))
        self.message_entry.bind("<Return>", self.send_message)
        
        ttk.Button(message_frame, text="Send", command=self.send_message).pack(side=tk.RIGHT)
        
        # Image frame (right side)
        image_frame = ttk.LabelFrame(split_frame, text="Image Transfer")
        image_frame.pack(side=tk.RIGHT, fill=tk.BOTH, expand=True, padx=(5, 0))
        
        # Image preview
        self.image_frame = ttk.Frame(image_frame, borderwidth=1, relief=tk.SUNKEN)
        self.image_frame.pack(fill=tk.BOTH, expand=True, pady=(0, 10))
        
        # No image label
        self.no_image_label = ttk.Label(self.image_frame, text="No Image Selected")
        self.no_image_label.pack(expand=True)
        
        # Image preview label
        self.image_label = ttk.Label(self.image_frame)
        
        # Image info
        self.image_info_var = tk.StringVar(value="No image selected")
        ttk.Label(image_frame, textvariable=self.image_info_var).pack(fill=tk.X, pady=(0, 5))
        
        # Image buttons
        image_buttons_frame = ttk.Frame(image_frame)
        image_buttons_frame.pack(fill=tk.X)
        
        ttk.Button(image_buttons_frame, text="Select Image", command=self.select_image).pack(side=tk.LEFT, padx=(0, 5))
        self.send_image_button = ttk.Button(image_buttons_frame, text="Send Image", command=self.send_image, state=tk.DISABLED)
        self.send_image_button.pack(side=tk.RIGHT)
        
        self.current_image_path = None
        self.compressed_image_data = None
        
    def update_port_list(self):
        """Update the list of available serial ports"""
        ports = [p.device for p in serial.tools.list_ports.comports()]
        self.port_combobox['values'] = ports
        if ports:
            self.port_combobox.current(0)
            
    def toggle_connection(self):
        """Connect to or disconnect from the selected serial port"""
        if not self.connected:
            port = self.port_combobox.get()
            if not port:
                messagebox.showerror("Error", "No port selected")
                return
                
            try:
                self.serial_port = serial.Serial(port, 115200, timeout=0.1)
                self.connected = True
                self.connect_button.config(text="Disconnect")
                self.status_var.set(f"Connected to {port}")
                
                # Start reading thread
                self.stop_thread = False
                self.serial_thread = threading.Thread(target=self.read_serial)
                self.serial_thread.daemon = True
                self.serial_thread.start()
                
                # Enable UI elements
                self.send_image_button.config(state=tk.NORMAL if self.compressed_image_data else tk.DISABLED)
                
            except Exception as e:
                messagebox.showerror("Connection Error", str(e))
        else:
            # Disconnect
            self.stop_thread = True
            if self.serial_thread:
                self.serial_thread.join(1.0)
                
            if self.serial_port:
                self.serial_port.close()
                self.serial_port = None
                
            self.connected = False
            self.connect_button.config(text="Connect")
            self.status_var.set("Not Connected")
            self.send_image_button.config(state=tk.DISABLED)
            
    def read_serial(self):
        """Thread function to read data from the serial port"""
        while not self.stop_thread:
            if self.serial_port and self.serial_port.is_open:
                try:
                    if self.serial_port.in_waiting:
                        data = self.serial_port.readline().decode('utf-8', errors='ignore').strip()
                        if data:
                            self.update_chat_display(f"ESP: {data}")
                except Exception as e:
                    self.update_chat_display(f"Error reading from serial: {str(e)}")
                    break
            time.sleep(0.1)
            
    def update_chat_display(self, message):
        """Update the chat display with a new message"""
        def _update():
            self.chat_display.config(state=tk.NORMAL)
            self.chat_display.insert(tk.END, message + "\n")
            self.chat_display.see(tk.END)
            self.chat_display.config(state=tk.DISABLED)
        self.root.after(0, _update)
            
    def send_message(self, event=None):
        """Send a message to the ESP8266"""
        message = self.message_entry.get().strip()
        if not message:
            return
            
        if not self.connected or not self.serial_port:
            messagebox.showerror("Error", "Not connected to device")
            return
            
        try:
            self.serial_port.write(f"text:{message}\n".encode('utf-8'))
            self.update_chat_display(f"You: {message}")
            self.message_entry.delete(0, tk.END)
        except Exception as e:
            messagebox.showerror("Send Error", str(e))
            
    def select_image(self):
        """Open a file dialog to select an image"""
        file_path = filedialog.askopenfilename(
            title="Select Image",
            filetypes=[
                ("Image files", "*.jpg *.jpeg *.png *.bmp *.gif"),
                ("All files", "*.*")
            ]
        )
        
        if not file_path:
            return
            
        self.current_image_path = file_path
        self.compress_and_preview_image(file_path)
        
    def compress_and_preview_image(self, image_path):
        """Compress the image to under 20KB and show a preview"""
        try:
            # Open and compress image
            image = Image.open(image_path)
            
            # Calculate aspect ratio
            width, height = image.size
            aspect_ratio = width / height
            
            # Resize for preview (maintaining aspect ratio)
            preview_width = 300
            preview_height = int(preview_width / aspect_ratio)
            
            # Create preview image
            preview_image = image.copy()
            preview_image.thumbnail((preview_width, preview_height))
            
            # Display preview
            photo = ImageTk.PhotoImage(preview_image)
            self.image_label.config(image=photo)
            self.image_label.image = photo  # Keep a reference
            
            # Replace the "No Image" label with the image
            self.no_image_label.pack_forget()
            self.image_label.pack(expand=True)
            
            # Compress image for sending
            max_size = 19 * 1024  # 19KB (leaving some room for headers)
            quality = 95
            output = io.BytesIO()
            
            # Try compressing with different quality settings until it's small enough
            while quality > 5:  # Don't go below quality 5
                output.seek(0)
                output.truncate(0)
                image.save(output, format='JPEG', quality=quality)
                if output.tell() <= max_size:
                    break
                quality -= 5
                
            # If it's still too big, resize the image
            current_width, current_height = image.size
            scale_factor = 1.0
            
            while output.tell() > max_size and scale_factor > 0.1:
                scale_factor *= 0.9
                new_width = int(current_width * scale_factor)
                new_height = int(current_height * scale_factor)
                
                resized_img = image.resize((new_width, new_height), Image.LANCZOS)
                
                output.seek(0)
                output.truncate(0)
                resized_img.save(output, format='JPEG', quality=quality)
            
            # Get final compressed image data
            self.compressed_image_data = output.getvalue()
            
            # Update image info
            original_size = os.path.getsize(image_path) / 1024  # KB
            compressed_size = len(self.compressed_image_data) / 1024  # KB
            compression_ratio = original_size / compressed_size if compressed_size > 0 else 0
            
            self.image_info_var.set(
                f"Original: {original_size:.1f}KB | "
                f"Compressed: {compressed_size:.1f}KB | "
                f"Ratio: {compression_ratio:.1f}x | "
                f"Quality: {quality}%"
            )
            
            # Enable send button if connected
            if self.connected:
                self.send_image_button.config(state=tk.NORMAL)
                
        except Exception as e:
            messagebox.showerror("Image Error", str(e))
            self.image_info_var.set(f"Error: {str(e)}")
            self.compressed_image_data = None
            self.send_image_button.config(state=tk.DISABLED)
            
    def send_image(self):
        """Send the compressed image to the ESP8266"""
        if not self.compressed_image_data:
            messagebox.showerror("Error", "No image compressed and ready to send")
            return
            
        if not self.connected or not self.serial_port:
            messagebox.showerror("Error", "Not connected to device")
            return
            
        try:
            # Start image upload process
            self.update_chat_display("Starting image upload...")
            
            # Send upload command
            self.serial_port.write(b"image:upload\n")
            time.sleep(0.5)  # Wait for ESP to enter upload mode
            
            # Send image size (2 bytes, little endian)
            size = len(self.compressed_image_data)
            self.serial_port.write(bytes([size & 0xFF, (size >> 8) & 0xFF]))
            
            # Send image data in chunks
            sent = 0
            chunk_size = 64  # Send in smaller chunks
            
            while sent < size:
                chunk = self.compressed_image_data[sent:sent+chunk_size]
                self.serial_port.write(chunk)
                sent += len(chunk)
                
                # Update progress
                if sent % 512 == 0 or sent == size:
                    progress = (sent / size) * 100
                    self.update_chat_display(f"Uploading image: {progress:.1f}% ({sent}/{size} bytes)")
                    
                time.sleep(0.01)  # Small delay to avoid overwhelming the serial buffer
                
            self.update_chat_display(f"Image upload complete: {size} bytes sent")
            
        except Exception as e:
            messagebox.showerror("Send Error", str(e))
            
if __name__ == "__main__":
    root = tk.Tk()
    app = ESPChatApp(root)
    root.mainloop() 