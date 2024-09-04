import time
from data.data_fetching import fetch_data
from data.data_preprocessing import preprocess_data
from models.model_training import make_predictions
from trading.trading_execution import execute_trades

def real_time_trading(symbols, db_config, models, lstm_models, interval=3600):
    while True:
        data = fetch_data(symbols, db_config)
        data = preprocess_data(data)
        for symbol in symbols:
            symbol_data = data[data['symbol'] == symbol]
            predictions = make_predictions(models[symbol], lstm_models[symbol], symbol_data)
            execute_trades(predictions, [symbol], symbol_data)
        time.sleep(interval)