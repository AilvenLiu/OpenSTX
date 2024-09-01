from db_config import read_db_config
from data_loader import AsyncDataLoader
from model_training import train_models, make_predictions
from realtime_trading import real_time_trading
from consecutive_learning import continuous_learning
from backtesting import backtest
import threading

def main():
    db_config = read_db_config()
    symbols = ["SPY", "QQQ", "XLK", "AAPL", "MSFT", "AMZN", "GOOGL", "TSLA", "NVDA", "META", "AMD", "ADBE", "CRM", "SHOP"]
    
    data_loader = AsyncDataLoader(symbols, db_config, load_from_memory=False)
    data_loader.start()
    
    try:
        models, lstm_models = train_models(data_loader)
        
        backtest_results = backtest(data_loader, models, lstm_models)
        print("Backtest results:", backtest_results)
        
        learning_thread = threading.Thread(target=continuous_learning, args=(symbols, db_config, models, lstm_models))
        learning_thread.start()
        
        trading_thread = threading.Thread(target=real_time_trading, args=(symbols, db_config, models, lstm_models))
        trading_thread.start()
        
        learning_thread.join()
        trading_thread.join()
        
    finally:
        data_loader.stop()

if __name__ == "__main__":
    main()