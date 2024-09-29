import random
import numpy as np
import pandas as pd
from sklearn.ensemble import RandomForestClassifier
from sklearn.preprocessing import StandardScaler

def generate_strategy():
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

def bollinger_bands(data, window=20, num_std_dev=2):
    rolling_mean = data['close'].rolling(window=window).mean()
    rolling_std = data['close'].rolling(window=window).std()
    upper_band = rolling_mean + (rolling_std * num_std_dev)
    lower_band = rolling_mean - (rolling_std * num_std_dev)
    return upper_band, lower_band

def macd(data, short_window=12, long_window=26, signal_window=9):
    short_ema = data['close'].ewm(span=short_window, adjust=False).mean()
    long_ema = data['close'].ewm(span=long_window, adjust=False).mean()
    macd_line = short_ema - long_ema
    signal_line = macd_line.ewm(span=signal_window, adjust=False).mean()
    return macd_line, signal_line

def rsi(data, window=14):
    delta = data['close'].diff()
    gain = (delta.where(delta > 0, 0)).rolling(window=window).mean()
    loss = (-delta.where(delta < 0, 0)).rolling(window=window).mean()
    rs = gain / loss
    rsi = 100 - (100 / (1 + rs))
    return rsi

def generate_trading_signals(predictions, actual_prices, data, short_window=40, long_window=100, ml_model=None, scaler=None):
    signals = []
    upper_band, lower_band = bollinger_bands(data)
    macd_line, signal_line = macd(data)
    rsi_values = rsi(data)
    
    features = []
    for i in range(len(predictions)):
        short_moving_avg = np.mean(actual_prices[max(0, i-short_window):i+1])
        long_moving_avg = np.mean(actual_prices[max(0, i-long_window):i+1])
        
        features.append([
            short_moving_avg,
            long_moving_avg,
            predictions[i],
            actual_prices[i],
            data['close'].iloc[i],
            upper_band.iloc[i],
            lower_band.iloc[i],
            macd_line.iloc[i],
            signal_line.iloc[i],
            rsi_values.iloc[i]
        ])
    
    features = np.array(features)
    features_scaled = scaler.transform(features)
    
    # Generate signals using the pre-trained model
    predictions_proba = ml_model.predict_proba(features_scaled)[:, 1]
    for prob in predictions_proba:
        if prob > 0.6:
            signals.append("buy")
        elif prob < 0.4:
            signals.append("sell")
        else:
            signals.append("hold")
    
    return signals