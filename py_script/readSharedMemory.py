import pandas as pd
import numpy as np
import struct
import mmap
import os

def read_shared_memory(shm_name: str, size: int) -> pd.DataFrame:
    # Open the shared memory object
    fd = os.open(shm_name, os.O_RDONLY)
    # Memory-map the file
    mem_map = mmap.mmap(fd, size, mmap.MAP_SHARED, mmap.PROT_READ)
    
    # Read the memory content
    content = mem_map.read(size)
    
    # Assume data is comma-separated values (CSV) format in memory
    # Convert to string and then to pandas DataFrame
    data_str = content.decode('utf-8').strip()
    data_lines = data_str.splitlines()

    # Assuming the first line is the header
    header = data_lines[0].split(',')
    rows = [line.split(',') for line in data_lines[1:]]
    
    # Create DataFrame
    df = pd.DataFrame(rows, columns=header)
    
    # Convert numeric columns from string to float
    for col in df.columns[1:]:
        df[col] = pd.to_numeric(df[col], errors='coerce')
    
    return df
