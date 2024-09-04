import xgboost as xgb
import lightgbm as lgb
from sklearn.model_selection import train_test_split, GridSearchCV
from sklearn.ensemble import VotingRegressor, RandomForestRegressor
import torch
import torch.nn as nn
import torch.optim as optim
from torch.utils.data import DataLoader, TensorDataset
from data.data_loader import AsyncDataLoader

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

def train_models(data_loader):
    models = {}
    lstm_models = {}

    for symbol, symbol_data in data_loader:
        X = symbol_data.drop(columns=['close', 'symbol'])
        y = symbol_data['close']
        X_train, X_test, y_train, y_test = train_test_split(X, y, test_size=0.2, shuffle=False)
        
        xgb_model = xgb.XGBRegressor(objective='reg:squarederror')
        lgb_model = lgb.LGBMRegressor(objective='regression')
        rf_model = RandomForestRegressor()
        
        param_grid = {
            'xgb__n_estimators': [50, 100],
            'xgb__learning_rate': [0.01, 0.1],
            'lgb__n_estimators': [50, 100],
            'lgb__learning_rate': [0.01, 0.1],
            'rf__n_estimators': [50, 100],
            'rf__max_depth': [5, 10]
        }
        
        ensemble_model = VotingRegressor(estimators=[('xgb', xgb_model), ('lgb', lgb_model), ('rf', rf_model)])
        grid_search = GridSearchCV(estimator=ensemble_model, param_grid=param_grid, cv=3, n_jobs=-1)
        grid_search.fit(X_train, y_train)
        
        best_model = grid_search.best_estimator_
        
        X_train_lstm = X_train.values.reshape((X_train.shape[0], 1, X_train.shape[1]))
        lstm_model = train_lstm_model(X_train_lstm, y_train.values, X_train.shape[1])
        
        models[symbol] = best_model
        lstm_models[symbol] = lstm_model
        
        print(f"Models trained successfully for {symbol}")
    
    return models, lstm_models

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

def make_predictions(model, lstm_model, data):
    X = data.drop(columns=['close', 'symbol'])
    X_lstm = X.values.reshape((X.shape[0], 1, X.shape[1]))
    
    ensemble_predictions = model.predict(X)
    lstm_model.eval()
    with torch.no_grad():
        lstm_predictions = lstm_model(torch.tensor(X_lstm, dtype=torch.float32)).numpy().flatten()
    
    predictions = (ensemble_predictions + lstm_predictions) / 2
    return predictions