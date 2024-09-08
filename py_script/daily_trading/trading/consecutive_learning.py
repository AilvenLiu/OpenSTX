import time
from data.data_fetching import fetch_data
from data.data_preprocessing import preprocess_data
from models.model_training import update_model, update_lstm_model, update_arima_model, update_garch_model, update_transformer_model
from config.db_config import read_db_config

def continuous_learning(symbols, db_config, models, lstm_models, arima_models, garch_models, transformer_models, interval=3600, window_size=252, prediction_days=5):
    while True:
        new_data = fetch_data(symbols, db_config)
        new_data = preprocess_data(new_data)
        for symbol in symbols:
            symbol_data = new_data[new_data['symbol'] == symbol]
            symbol_data = symbol_data.tail(window_size)  # Use the last `window_size` days of data
            models[symbol] = update_model(models[symbol], symbol_data)
            lstm_models[symbol] = update_lstm_model(lstm_models[symbol], symbol_data)
            arima_models[symbol] = update_arima_model(arima_models[symbol], symbol_data)
            garch_models[symbol] = update_garch_model(garch_models[symbol], symbol_data)
            transformer_models[symbol] = update_transformer_model(transformer_models[symbol], symbol_data)
        print("Models updated successfully")
        time.sleep(interval)