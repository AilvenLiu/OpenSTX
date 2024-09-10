import os
import sys
from config.db_config import read_db_config
from data.data_loader import AsyncDataLoader
from models.model_training import train_models, make_predictions
from trading.realtime_trading import real_time_trading
from trading.consecutive_learning import continuous_learning
from trading.backtesting import backtest
import threading

def check_libomp():
    if sys.platform == "darwin":  # Check if running on macOS
        if not os.path.exists("/opt/homebrew/opt/libomp/lib/libomp.dylib"):
            print("Warning: libomp is not installed. Please install it before executing:")
            print("brew install libomp")
            sys.exit(1)

def main():
    check_libomp()

    db_config = read_db_config()
    symbols = ["SPY", "QQQ", "XLK", "AAPL", "MSFT", "AMZN", "GOOGL", "TSLA", "NVDA", "META", "AMD", "ADBE", "CRM", "SHOP"]
    
    data_loader = AsyncDataLoader(symbols, db_config, load_from_memory=False)
    data_loader.start()
    
    try:
        models, lstm_models, arima_models, garch_models, transformer_models, ml_models = train_models(data_loader, prediction_days=5)
        
        backtest_results, best_model = backtest(data_loader, models, lstm_models, arima_models, garch_models, transformer_models, ml_models, prediction_days=5)
        print("Backtest results:", backtest_results)
        print("Best model:", best_model)
        
        learning_thread = threading.Thread(target=continuous_learning, args=(symbols, db_config, models, lstm_models, arima_models, garch_models, transformer_models, ml_models, 3600, 252, 5))
        learning_thread.start()
        
        trading_thread = threading.Thread(target=real_time_trading, args=(symbols, db_config, models, lstm_models, arima_models, garch_models, transformer_models, ml_models, 3600, 252, 5))
        trading_thread.start()
        
        learning_thread.join()
        trading_thread.join()
        
    finally:
        data_loader.stop()

if __name__ == "__main__":
    main()