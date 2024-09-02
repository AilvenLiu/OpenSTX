import time
from data.data_fetching import fetch_data
from data.data_preprocessing import preprocess_data
from models.model_training import update_model, update_lstm_model
from config.db_config import read_db_config

def continuous_learning(symbols, db_config, models, lstm_models, interval=3600):
    while True:
        new_data = fetch_data(symbols, db_config)
        new_data = preprocess_data(new_data)
        for symbol in symbols:
            symbol_data = new_data[new_data['symbol'] == symbol]
            models[symbol] = update_model(models[symbol], symbol_data)
            lstm_models[symbol] = update_lstm_model(lstm_models[symbol], symbol_data)
        print("Models updated successfully")
        time.sleep(interval)