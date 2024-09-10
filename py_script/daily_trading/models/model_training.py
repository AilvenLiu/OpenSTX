import xgboost as xgb
import lightgbm as lgb
from sklearn.model_selection import GridSearchCV
from sklearn.ensemble import VotingRegressor, RandomForestRegressor, RandomForestClassifier
from sklearn.preprocessing import StandardScaler
import torch
import torch.nn as nn
import torch.optim as optim
from torch.utils.data import DataLoader, TensorDataset
from statsmodels.tsa.arima.model import ARIMA
from arch import arch_model
from tqdm import tqdm
from strategies.combined_strategy import bollinger_bands, macd, rsi
from lightgbm import early_stopping
from sklearn.model_selection import train_test_split

# Check if MPS (Metal Performance Shaders) is available for Apple M1
device = torch.device("mps") if torch.backends.mps.is_available() else torch.device("cpu")

class LSTMModel(nn.Module):
    def __init__(self, input_size, hidden_size=50, num_layers=2, output_size=1):
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

class TransformerModel(nn.Module):
    def __init__(self, input_size, num_heads=2, num_layers=2, output_size=1):
        super(TransformerModel, self).__init__()
        self.transformer = nn.Transformer(input_size, num_heads, num_layers)
        self.fc = nn.Linear(input_size, output_size)

    def forward(self, x):
        out = self.transformer(x)
        out = self.fc(out[:, -1, :])
        return out

def train_lstm_model(X_train, y_train, input_size, hidden_size=50, num_layers=2, epochs=10, lr=0.001):
    model = LSTMModel(input_size=input_size, hidden_size=hidden_size, num_layers=num_layers, output_size=1).to(device)
    criterion = nn.MSELoss()
    optimizer = optim.Adam(model.parameters(), lr=lr)
    scheduler = optim.lr_scheduler.StepLR(optimizer, step_size=5, gamma=0.1)
    
    dataset = TensorDataset(torch.tensor(X_train, dtype=torch.float32), torch.tensor(y_train, dtype=torch.float32))
    dataloader = DataLoader(dataset, batch_size=32, shuffle=True)
    
    model.train()
    for epoch in tqdm(range(epochs), desc="Training LSTM Model"):
        for X_batch, y_batch in dataloader:
            X_batch, y_batch = X_batch.to(device), y_batch.to(device)
            optimizer.zero_grad()
            outputs = model(X_batch)
            loss = criterion(outputs, y_batch.unsqueeze(1))
            loss.backward()
            optimizer.step()
        scheduler.step()
    
    return model

def train_transformer_model(X_train, y_train, input_size, num_heads=2, num_layers=2, epochs=10, lr=0.001):
    model = TransformerModel(input_size=input_size, num_heads=num_heads, num_layers=num_layers, output_size=1).to(device)
    criterion = nn.MSELoss()
    optimizer = optim.Adam(model.parameters(), lr=lr)
    scheduler = optim.lr_scheduler.StepLR(optimizer, step_size=5, gamma=0.1)
    
    dataset = TensorDataset(torch.tensor(X_train, dtype=torch.float32), torch.tensor(y_train, dtype=torch.float32))
    dataloader = DataLoader(dataset, batch_size=32, shuffle=True)
    
    model.train()
    for epoch in tqdm(range(epochs), desc="Training Transformer Model"):
        for X_batch, y_batch in dataloader:
            X_batch, y_batch = X_batch.to(device), y_batch.to(device)
            optimizer.zero_grad()
            outputs = model(X_batch)
            loss = criterion(outputs, y_batch.unsqueeze(1))
            loss.backward()
            optimizer.step()
        scheduler.step()
    
    return model

def train_models(data_loader, lstm_params={}, transformer_params={}, grid_params={}, window_size=252, prediction_days=5):
    models = {}
    lstm_models = {}
    arima_models = {}
    garch_models = {}
    transformer_models = {}
    ml_models = {}

    for symbol, symbol_data in tqdm(data_loader, desc="Training Models"):
        X = symbol_data.drop(columns=['close', 'symbol'])
        y = symbol_data['close']
        
        # Rolling window approach
        for start in range(len(X) - window_size - prediction_days):
            end = start + window_size
            X_train, X_test = X[start:end], X[end:end+prediction_days]
            y_train, y_test = y[start:end], y[end:end+prediction_days]
            
            xgb_model = xgb.XGBRegressor(objective='reg:squarederror', n_estimators=200, learning_rate=0.05, early_stopping_rounds=10)
            lgb_model = lgb.LGBMRegressor(objective='regression', n_estimators=200, learning_rate=0.05, 
                                          num_leaves=31, feature_fraction=0.8, bagging_fraction=0.8, 
                                          bagging_freq=5, min_child_samples=20)
            rf_model = RandomForestRegressor()
            
            param_grid = grid_params if grid_params else {
                'xgb__n_estimators': [100, 200],
                'xgb__learning_rate': [0.01, 0.05],
                'lgb__n_estimators': [100, 200],
                'lgb__learning_rate': [0.01, 0.05],
                'lgb__num_leaves': [31, 63],
                'rf__n_estimators': [50, 100],
                'rf__max_depth': [5, 10]
            }
            
            ensemble_model = VotingRegressor(estimators=[('xgb', xgb_model), ('lgb', lgb_model), ('rf', rf_model)])
            grid_search = GridSearchCV(estimator=ensemble_model, param_grid=param_grid, cv=3, n_jobs=-1)
            X_train, X_val, y_train, y_val = train_test_split(X_train, y_train, test_size=0.2, random_state=42)
            grid_search.fit(X_train, y_train, 
                            lgb__eval_set=[(X_val, y_val)],
                            lgb__eval_metric='l2',
                            lgb__early_stopping_rounds=10,
                            lgb__verbose=0,
                            xgb__eval_set=[(X_val, y_val)],
                            xgb__eval_metric='rmse',
                            xgb__early_stopping_rounds=10,
                            xgb__verbose=0)
            best_model = grid_search.best_estimator_
            
            X_train_lstm = X_train.values.reshape((X_train.shape[0], 1, X_train.shape[1]))
            lstm_model = train_lstm_model(X_train_lstm, y_train.values, X_train.shape[1], **lstm_params)
            
            transformer_model = train_transformer_model(X_train_lstm, y_train.values, X_train.shape[1], **transformer_params)
            
            arima_model = ARIMA(y_train, order=(5, 1, 0)).fit()
            garch_model = arch_model(y_train, vol='Garch', p=1, q=1).fit()
            
            # Create features using technical indicators
            upper_band, lower_band = bollinger_bands(symbol_data)
            macd_line, signal_line = macd(symbol_data)
            rsi_values = rsi(symbol_data)
            
            features = []
            for i in range(len(X_train)):
                features.append([
                    X_train.iloc[i].mean(),
                    upper_band.iloc[i],
                    lower_band.iloc[i],
                    macd_line.iloc[i],
                    signal_line.iloc[i],
                    rsi_values.iloc[i]
                ])
            
            features = np.array(features)
            scaler = StandardScaler()
            features_scaled = scaler.fit_transform(features)
            
            # Train a machine learning model (e.g., RandomForestClassifier)
            ml_model = RandomForestClassifier(n_estimators=100, random_state=42)
            ml_model.fit(features_scaled, (y_train > y_train.shift(1)).astype(int).fillna(0))
            
            models[symbol] = best_model
            lstm_models[symbol] = lstm_model
            arima_models[symbol] = arima_model
            garch_models[symbol] = garch_model
            transformer_models[symbol] = transformer_model
            ml_models[symbol] = (ml_model, scaler)
            
            print(f"Models trained successfully for {symbol}")
    
    return models, lstm_models, arima_models, garch_models, transformer_models, ml_models

def update_model(model, new_data):
    X_new = new_data.drop(columns=['close', 'symbol'])
    y_new = new_data['close']
    model.fit(X_new, y_new)
    return model

def update_lstm_model(lstm_model, new_data, lstm_params={}):
    X_new = new_data.drop(columns=['close', 'symbol']).values.reshape((new_data.shape[0], 1, new_data.shape[1] - 2))
    y_new = new_data['close'].values
    lstm_model = train_lstm_model(X_new, y_new, X_new.shape[2], **lstm_params)
    return lstm_model

def update_arima_model(arima_model, new_data):
    y_new = new_data['close'].values
    arima_model = ARIMA(y_new, order=(5, 1, 0)).fit()
    return arima_model

def update_garch_model(garch_model, new_data):
    y_new = new_data['close'].values
    garch_model = arch_model(y_new, vol='Garch', p=1, q=1).fit()
    return garch_model

def update_transformer_model(transformer_model, new_data, transformer_params={}):
    X_new = new_data.drop(columns=['close', 'symbol']).values.reshape((new_data.shape[0], 1, new_data.shape[1] - 2))
    y_new = new_data['close'].values
    transformer_model = train_transformer_model(X_new, y_new, X_new.shape[2], **transformer_params)
    return transformer_model

def update_ml_model(ml_model, scaler, new_data):
    X_new = new_data.drop(columns=['close', 'symbol'])
    y_new = new_data['close']
    upper_band, lower_band = bollinger_bands(new_data)
    macd_line, signal_line = macd(new_data)
    rsi_values = rsi(new_data)
    
    features = []
    for i in range(len(X_new)):
        features.append([
            X_new.iloc[i].mean(),
            upper_band.iloc[i],
            lower_band.iloc[i],
            macd_line.iloc[i],
            signal_line.iloc[i],
            rsi_values.iloc[i]
        ])
    
    features = np.array(features)
    features_scaled = scaler.transform(features)
    ml_model.fit(features_scaled, (y_new > y_new.shift(1)).astype(int).fillna(0))
    return ml_model, scaler

def make_predictions(model, lstm_model, arima_model, garch_model, transformer_model, ml_model, scaler, data, prediction_days=5):
    X = data.drop(columns=['close', 'symbol'])
    X_lstm = X.values.reshape((X.shape[0], 1, X.shape[1]))
    
    ensemble_predictions = model.predict(X)
    lstm_model.eval()
    with torch.no_grad():
        lstm_predictions = lstm_model(torch.tensor(X_lstm, dtype=torch.float32).to(device)).cpu().numpy().flatten()
    
    arima_predictions = arima_model.forecast(steps=prediction_days).values
    garch_predictions = garch_model.forecast(horizon=prediction_days).mean['h.1'].values
    transformer_model.eval()
    with torch.no_grad():
        transformer_predictions = transformer_model(torch.tensor(X_lstm, dtype=torch.float32).to(device)).cpu().numpy().flatten()
    
    # Use the machine learning model for predictions
    X_scaled = scaler.transform(X)
    ml_predictions = ml_model.predict_proba(X_scaled)[:, 1]  # Probability of price increase
    
    predictions = (ensemble_predictions[-prediction_days:] + lstm_predictions[-prediction_days:] + arima_predictions + garch_predictions + transformer_predictions[-prediction_days:] + ml_predictions[-prediction_days:]) / 6
    return predictions