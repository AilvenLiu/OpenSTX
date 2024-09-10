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

def backtest(data_loader, models, lstm_models, arima_models, garch_models, transformer_models, ml_models, window_size=252, prediction_days=5):
    results = {}
    for symbol, symbol_data in data_loader:
        X = symbol_data.drop(columns=['close', 'symbol'])
        y = symbol_data['close']
        
        # Rolling window approach
        for start in range(len(X) - window_size - prediction_days):
            end = start + window_size
            X_train, X_test = X[start:end], X[end:end+prediction_days]
            y_train, y_test = y[start:end], y[end:end+prediction_days]
            
            # Make predictions on the test set
            predictions = make_predictions(models[symbol], lstm_models[symbol], arima_models[symbol], garch_models[symbol], transformer_models[symbol], ml_models[symbol][0], ml_models[symbol][1], X_test, prediction_days)
            df = pd.DataFrame({'actual': y_test.values[:prediction_days], 'predicted': predictions})
            
            # Calculate performance metrics
            metrics = calculate_metrics(df)
            results[symbol] = metrics
    
    # Select the best model based on Sharpe ratio
    best_model = max(results, key=lambda x: results[x]['sharpe_ratio'])
    return results, best_model