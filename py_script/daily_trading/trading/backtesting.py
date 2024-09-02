import pandas as pd
from models.model_training import make_predictions
from sklearn.metrics import mean_squared_error, r2_score
import numpy as np

def calculate_metrics(df):
    df['returns'] = df['actual'].pct_change()
    df['strategy_returns'] = df['returns'] * (df['predicted'].shift(1) > df['actual'].shift(1))
    df['cumulative_returns'] = (1 + df['returns']).cumprod()
    df['cumulative_strategy_returns'] = (1 + df['strategy_returns']).cumprod()
    
    # Calculate Sharpe ratio
    sharpe_ratio = np.mean(df['strategy_returns']) / np.std(df['strategy_returns']) * np.sqrt(252)
    
    # Calculate maximum drawdown
    cumulative = df['cumulative_strategy_returns']
    drawdown = cumulative / cumulative.cummax() - 1
    max_drawdown = drawdown.min()
    
    return {
        'cumulative_returns': df['cumulative_returns'].iloc[-1],
        'cumulative_strategy_returns': df['cumulative_strategy_returns'].iloc[-1],
        'sharpe_ratio': sharpe_ratio,
        'max_drawdown': max_drawdown,
        'mean_squared_error': mean_squared_error(df['actual'], df['predicted']),
        'r2_score': r2_score(df['actual'], df['predicted'])
    }

def backtest(data_loader, models, lstm_models):
    results = {}
    for symbol, symbol_data in data_loader:
        # Split data into training and testing sets
        train_size = int(len(symbol_data) * 0.8)
        train_data = symbol_data[:train_size]
        test_data = symbol_data[train_size:]
        
        # Make predictions on the test set
        predictions = make_predictions(models[symbol], lstm_models[symbol], test_data)
        df = pd.DataFrame({'actual': test_data['close'], 'predicted': predictions})
        
        # Calculate performance metrics
        metrics = calculate_metrics(df)
        results[symbol] = metrics
    
    return results