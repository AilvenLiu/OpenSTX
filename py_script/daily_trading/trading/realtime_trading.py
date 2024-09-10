import time
from data.data_fetching import fetch_data
from data.data_preprocessing import preprocess_data
from models.model_training import make_predictions
from trading.trading_execution import execute_trades
from strategies.combined_strategy import generate_trading_signals

def real_time_trading(symbols, db_config, models, lstm_models, arima_models, garch_models, transformer_models, ml_models, interval=3600, window_size=252, prediction_days=5):
    while True:
        data = fetch_data(symbols, db_config)
        data = preprocess_data(data)
        for symbol in symbols:
            symbol_data = data[data['symbol'] == symbol]
            symbol_data = symbol_data.tail(window_size)  # Use the last `window_size` days of data
            predictions = make_predictions(models[symbol], lstm_models[symbol], arima_models[symbol], garch_models[symbol], transformer_models[symbol], ml_models[symbol][0], ml_models[symbol][1], symbol_data, prediction_days)
            actual_prices = symbol_data['close'].values
            signals = generate_trading_signals(predictions, actual_prices, symbol_data, ml_model=ml_models[symbol][0], scaler=ml_models[symbol][1])
            execute_trades(signals, [symbol], symbol_data)
        time.sleep(interval)