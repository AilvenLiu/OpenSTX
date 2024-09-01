import pandas as pd
from model_training import make_predictions

def backtest(data_loader, models, lstm_models):
    results = {}
    for symbol, symbol_data in data_loader:
        predictions = make_predictions(models[symbol], lstm_models[symbol], symbol_data)
        df = pd.DataFrame({'actual': symbol_data['close'], 'predicted': predictions})
        df['returns'] = df['actual'].pct_change()
        df['strategy_returns'] = df['returns'] * (df['predicted'].shift(1) > df['actual'].shift(1))
        df['cumulative_returns'] = (1 + df['returns']).cumprod()
        df['cumulative_strategy_returns'] = (1 + df['strategy_returns']).cumprod()
        results[symbol] = df
    return results