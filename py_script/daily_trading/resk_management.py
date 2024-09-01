def apply_risk_management(predictions, data, max_position_size=10000):
    risk_adjusted_predictions = []
    for prediction, symbol in zip(predictions, data['symbol'].unique()):
        current_price = data[data['symbol'] == symbol]['close'].iloc[-1]
        position_size = min(max_position_size, prediction * current_price)
        risk_adjusted_predictions.append(position_size)
    return risk_adjusted_predictions

if __name__ == "__main__":
    from data_fetching import fetch_data
    from data_preprocessing import preprocess_data
    from db_config import read_db_config
    
    db_config = read_db_config()
    symbols = ["SPY", "QQQ", "XLK", "AAPL", "MSFT", "AMZN", "GOOGL", "TSLA", "NVDA", "META", "AMD", "ADBE", "CRM", "SHOP"]
    data = fetch_data(symbols, db_config)
    data = preprocess_data(data)
    predictions = [100, 200, 150, 300, 250, 400, 350, 450, 500, 550, 600, 650, 700, 750]
    risk_adjusted_predictions = apply_risk_management(predictions, data)
    print(risk_adjusted_predictions)