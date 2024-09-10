import configparser
import os
from pathlib import Path

def read_db_config(filename='alicloud_db.ini', section='cloud'):
    base_path = Path(__file__).resolve().parents[3]
    full_path = base_path / 'conf' / filename
    
    parser = configparser.ConfigParser(interpolation=None)
    parser.read(full_path)
    
    print(f"Reading config file from: {full_path}")
    print(f"Available sections: {parser.sections()}")
    
    db_config = {}
    if parser.has_section(section):
        params = parser.items(section)
        for param in params:
            db_config[param[0]] = param[1]
    else:
        raise Exception(f'Section [{section}] not found in the {full_path}')
    
    return db_config