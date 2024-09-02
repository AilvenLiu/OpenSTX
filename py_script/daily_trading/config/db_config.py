import configparser

def read_db_config(filename='alicloud_db.ini', section='cloud'):
    parser = configparser.ConfigParser()
    parser.read(filename)
    
    db_config = {}
    if parser.has_section(section):
        params = parser.items(section)
        for param in params:
            db_config[param[0]] = param[1]
    else:
        raise Exception(f'Section {section} not found in the {filename} file')
    
    return db_config