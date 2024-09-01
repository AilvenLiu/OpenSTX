import configparser
import psycopg2
import pandas as pd
import xgboost as xgb
import lightgbm as lgb
from sklearn.model_selection import train_test_split
from sklearn.ensemble import VotingRegressor, RandomForestRegressor
import torch
import torch.nn as nn
import torch.optim as optim
from torch.utils.data import DataLoader, TensorDataset
from ib_insync import *
import csv
from datetime import datetime

# Read database configuration from ini file
def read_db_config(filename='alicloud_db.ini', section='cloud'):
    parser = configparser.ConfigParser()
    parser.read(filename)
    
    db_config = {}
    if parser.has_section(section):
        params = parser.items(section)
        for param in params:
            db_config[param[0]] = param[1]
    else:
        raise Exception(f'Section {section} not found in the {filename} file')
    
    return db_config

db_config = read_db_config()

# Fetch data from TimescaleDB in chunks
def fetch_data(symbols, db_config, chunk_size=10000):
    conn = psycopg2.connect(**db_config)
    query = f"""
    SELECT * FROM daily_data
    WHERE symbol IN ({','.join([f"'{symbol}'" for symbol in symbols])})
    ORDER BY date DESC;
    """
    chunks = []
    for chunk in pd.read_sql(query, conn, chunksize=chunk_size):
        chunks.append(chunk)
    conn.close()
    df = pd.concat(chunks, ignore_index=True)
    return df

# Preprocess the data
def preprocess_data(df):
    df['date'] = pd.to_datetime(df['date'])
    df.set_index('date', inplace=True)
    df.sort_index(inplace=True)
    df.dropna(inplace=True)
    return df

# Define LSTM model
class LSTMModel(nn.Module):
    def __init__(self, input_size, hidden_size, num_layers, output_size):
        super(LSTMModel, self).__init__()
        self.hidden_size = hidden_size
        self.num_layers = num_layers
        self.lstm = nn.LSTM(input_size, hidden_size, num_layers, batch_first=True)
        self.fc = nn.Linear(hidden_size, output_size)

    def forward(self, x):
        h0 = torch.zeros(self.num_layers, x.size(0), self.hidden_size).to(x.device)
        c0 = torch.zeros(self.num_layers, x.size(0), self.hidden_size).to(x.device)
        out, _ = self.lstm(x, (h0, c0))
        out = self.fc(out[:, -1, :])
        return out

def train_lstm_model(X_train, y_train, input_size):
    model = LSTMModel(input_size=input_size, hidden_size=50, num_layers=2, output_size=1)
    criterion = nn.MSELoss()
    optimizer = optim.Adam(model.parameters(), lr=0.001)
    
    dataset = TensorDataset(torch.tensor(X_train, dtype=torch.float32), torch.tensor(y_train, dtype=torch.float32))
    dataloader = DataLoader(dataset, batch_size=32, shuffle=True)
    
    model.train()
    for epoch in range(10):
        for X_batch, y_batch in dataloader:
            optimizer.zero_grad()
            outputs = model(X_batch)
            loss = criterion(outputs, y_batch.unsqueeze(1))
            loss.backward()
            optimizer.step()
    
    return model

# Train the models and create an ensemble
def train_models(data):
    X = data.drop(columns=['close', 'symbol'])
    y = data['close']
    X_train, X_test, y_train, y_test = train_test_split(X, y, test_size=0.2, shuffle=False)
    
    xgb_model = xgb.XGBRegressor(objective='reg:squarederror', n_estimators=100, learning_rate=0.1, max_depth=5)
    lgb_model = lgb.LGBMRegressor(objective='regression', n_estimators=100, learning_rate=0.1, max_depth=5)
    rf_model = RandomForestRegressor(n_estimators=100, max_depth=5)
    
    xgb_model.fit(X_train, y_train)
    lgb_model.fit(X_train, y_train)
    rf_model.fit(X_train, y_train)
    
    # Reshape data for LSTM
    X_train_lstm = X_train.values.reshape((X_train.shape[0], 1, X_train.shape[1]))
    lstm_model = train_lstm_model(X_train_lstm, y_train.values, X_train.shape[1])
    
    ensemble_model = VotingRegressor(estimators=[('xgb', xgb_model), ('lgb', lgb_model), ('rf', rf_model)])
    ensemble_model.fit(X_train, y_train)
    
    return ensemble_model, lstm_model

# Update the model with new data
def update_model(model, new_data):
    X_new = new_data.drop(columns=['close', 'symbol'])
    y_new = new_data['close']
    model.fit(X_new, y_new)
    return model

def update_lstm_model(lstm_model, new_data):
    X_new = new_data.drop(columns=['close', 'symbol']).values.reshape((new_data.shape[0], 1, new_data.shape[1] - 2))
    y_new = new_data['close'].values
    lstm_model = train_lstm_model(X_new, y_new, X_new.shape[2])
    return lstm_model

# Make predictions
def make_predictions(model, lstm_model, data):
    X = data.drop(columns=['close', 'symbol'])
    X_lstm = X.values.reshape((X.shape[0], 1, X.shape[1]))
    
    ensemble_predictions = model.predict(X)
    lstm_model.eval()
    with torch.no_grad():
        lstm_predictions = lstm_model(torch.tensor(X_lstm, dtype=torch.float32)).numpy().flatten()
    
    # Combine predictions (simple average for demonstration)
    predictions = (ensemble_predictions + lstm_predictions) / 2
    return predictions

# Execute trades using IB TWS API
def execute_trades(predictions, symbols, data):
    ib = IB()
    ib.connect('127.0.0.1', 7497, clientId=1)
    
    trades = []
    for symbol, prediction in zip(symbols, predictions):
        contract = Stock(symbol, 'SMART', 'USD')
        ib.qualifyContracts(contract)
        
        # Example: Buy if prediction is higher than current price
        current_price = data[data['symbol'] == symbol]['close'].iloc[-1]
        if prediction > current_price:
            order = MarketOrder('BUY', 10)
            trade = ib.placeOrder(contract, order)
            trades.append(trade)
            ib.sleep(1)
            print(f"Executed trade for {symbol}: {trade}")

    ib.disconnect()
    return trades

# Record trading information
def record_trades(trades):
    with open('trading_log.csv', mode='a') as file:
        writer = csv.writer(file)
        for trade in trades:
            writer.writerow([datetime.now(), trade.contract.symbol, trade.order.action, trade.orderStatus.filled, trade.orderStatus.avgFillPrice])

# Main function to run the entire process
def main():
    symbols = ["SPY", "QQQ", "XLK", "AAPL", "MSFT", "AMZN", "GOOGL", "TSLA", "NVDA", "META", "AMD", "ADBE", "CRM", "SHOP"]
    data = fetch_data(symbols, db_config)
    data = preprocess_data(data)
    model, lstm_model = train_models(data)
    
    # Fetch new data and update the models
    new_data = fetch_data(symbols, db_config)
    new_data = preprocess_data(new_data)
    model = update_model(model, new_data)
    lstm_model = update_lstm_model(lstm_model, new_data)
    
    # Make predictions and execute trades
    predictions = make_predictions(model, lstm_model, data)
    trades = execute_trades(predictions, symbols, data)
    
    # Record trades
    record_trades(trades)

if __name__ == "__main__":
    main()