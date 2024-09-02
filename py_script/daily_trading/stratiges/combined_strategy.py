import random

def generate_strategy():
    # Example: Randomly generate a simple moving average strategy
    short_window = random.randint(5, 20)
    long_window = random.randint(20, 50)
    return lambda data: data['close'].rolling(window=short_window).mean() > data['close'].rolling(window=long_window).mean()

def evaluate_strategy(strategy, data):
    signals = strategy(data)
    returns = data['close'].pct_change()
    strategy_returns = returns * signals.shift(1)
    return strategy_returns.cumsum().iloc[-1]

def select_best_strategy(strategies, data):
    best_strategy = max(strategies, key=lambda s: evaluate_strategy(s, data))
    return best_strategy

if __name__ == "__main__":
    from data.data_fetching import fetch_data
    from data.data_preprocessing import preprocess_data
    from config.db_config import read_db_config
    
    db_config = read_db_config()
    data = fetch_data(["SPY"], db_config)
    data = preprocess_data(data)
    strategies = [generate_strategy() for _ in range(10)]
    best_strategy = select_best_strategy(strategies, data)
    print("Best strategy selected")