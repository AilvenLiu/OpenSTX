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
import warnings
import logging
import pandas as pd

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

# Check if MPS (Metal Performance Shaders) is available for Apple M1
device = torch.device("mps") if torch.backends.mps.is_available() else torch.device("cpu")

class LSTMModel(nn.Module):
    def __init__(self, input_size, hidden_size=256, num_layers=2, output_size=1):
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
        return out.squeeze(-1)

class TransformerModel(nn.Module):
    def __init__(self, input_size, num_heads=2, num_layers=2, output_size=1):
        super(TransformerModel, self).__init__()
        self.input_size = input_size
        self.embed_dim = (input_size // num_heads) * num_heads  # Ensure divisibility
        self.transformer = nn.Transformer(d_model=self.embed_dim, nhead=num_heads, num_encoder_layers=num_layers)
        self.fc = nn.Linear(self.embed_dim, output_size)

    def forward(self, x):
        # Adjust input size if necessary
        if x.size(2) != self.embed_dim:
            x = nn.functional.pad(x, (0, self.embed_dim - x.size(2)))
        # Transformer expects input shape (seq_len, batch_size, input_size)
        x = x.permute(1, 0, 2)
        out = self.transformer(x, x)  # Using the same input for src and tgt
        out = out[-1, :, :]  # Take the last sequence element
        out = self.fc(out)
        return out.squeeze(-1)

def train_lstm_model(X, y, input_size, hidden_size=256, num_layers=2, output_size=1, learning_rate=0.001, batch_size=32, num_epochs=10):
    X_train, X_val, y_train, y_val = train_test_split(X, y, test_size=0.2, random_state=42)
    train_dataset = TensorDataset(torch.FloatTensor(X_train), torch.FloatTensor(y_train))
    val_dataset = TensorDataset(torch.FloatTensor(X_val), torch.FloatTensor(y_val))
    train_loader = DataLoader(train_dataset, batch_size=batch_size, shuffle=True)
    val_loader = DataLoader(val_dataset, batch_size=batch_size)

    model = LSTMModel(input_size, hidden_size, num_layers, output_size).to(device)
    criterion = nn.MSELoss()
    optimizer = optim.Adam(model.parameters(), lr=learning_rate)

    for epoch in tqdm(range(num_epochs), desc="Training LSTM Model"):
        model.train()
        train_loss = 0
        for batch_X, batch_y in train_loader:
            batch_X, batch_y = batch_X.to(device), batch_y.to(device)
            optimizer.zero_grad()
            outputs = model(batch_X)
            loss = criterion(outputs, batch_y)
            loss.backward()
            optimizer.step()
            train_loss += loss.item()

        model.eval()
        val_loss = 0
        with torch.no_grad():
            for batch_X, batch_y in val_loader:
                batch_X, batch_y = batch_X.to(device), batch_y.to(device)
                outputs = model(batch_X)
                loss = criterion(outputs, batch_y)
                val_loss += loss.item()

        logger.info(f'Epoch [{epoch+1}/{num_epochs}], Train Loss: {train_loss/len(train_loader):.4f}, Val Loss: {val_loss/len(val_loader):.4f}')

    return model

def train_transformer_model(X_train, y_train, input_size, num_heads=2, num_layers=2, epochs=10, lr=0.001):
    device = torch.device("mps") if torch.backends.mps.is_available() else torch.device("cuda" if torch.cuda.is_available() else "cpu")
    model = TransformerModel(input_size=input_size, num_heads=num_heads, num_layers=num_layers, output_size=1).to(device)
    criterion = nn.MSELoss()
    optimizer = optim.Adam(model.parameters(), lr=lr)
    scheduler = optim.lr_scheduler.StepLR(optimizer, step_size=5, gamma=0.1)

    X_train_tensor = torch.tensor(X_train, dtype=torch.float32).to(device)
    y_train_tensor = torch.tensor(y_train, dtype=torch.float32).to(device)
    dataset = TensorDataset(X_train_tensor, y_train_tensor)
    dataloader = DataLoader(dataset, batch_size=32, shuffle=True)

    model.train()
    for epoch in range(epochs):
        for X_batch, y_batch in dataloader:
            optimizer.zero_grad()
            outputs = model(X_batch)
            loss = criterion(outputs, y_batch)
            loss.backward()
            optimizer.step()
        scheduler.step()

    return model

def train_ensemble_model(X, y, grid_params):
    xgb_model = xgb.XGBRegressor(objective='reg:squarederror', n_estimators=100, learning_rate=0.1, max_depth=3)
    lgb_model = lgb.LGBMRegressor(
        objective='regression',
        n_estimators=50,
        learning_rate=0.05,
        num_leaves=15,
        max_depth=5,
        min_child_samples=20,
        subsample=0.8,
        colsample_bytree=0.8,
        reg_alpha=0.1,
        reg_lambda=0.1,
        n_jobs=-1,
        verbose=-1
    )
    rf_model = RandomForestRegressor(n_estimators=100, max_depth=5)
    
    param_grid = grid_params if grid_params else {
        'xgb__n_estimators': [50, 100],
        'xgb__learning_rate': [0.01, 0.05],
        'lgb__n_estimators': [30, 50],
        'lgb__learning_rate': [0.01, 0.05],
        'lgb__num_leaves': [7, 15],
        'rf__n_estimators': [50, 100],
        'rf__max_depth': [3, 5]
    }

    ensemble_model = VotingRegressor(estimators=[('xgb', xgb_model), ('lgb', lgb_model), ('rf', rf_model)])
    grid_search = GridSearchCV(estimator=ensemble_model, param_grid=param_grid, cv=3, n_jobs=-1, verbose=0)
    
    try:
        grid_search.fit(X, y)
        best_model = grid_search.best_estimator_
    except Exception as e:
        logger.error(f"Error training ensemble model: {str(e)}")
        best_model = None
    
    return best_model

def train_ml_model(X, y):
    upper_band, lower_band = bollinger_bands(X)
    macd_line, signal_line = macd(X)
    rsi_values = rsi(X)
    
    features = []
    for i in range(len(X)):
        features.append([
            X.iloc[i].mean(),
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
    ml_model.fit(features_scaled, (y > y.shift(1)).astype(int).fillna(0))
    
    return ml_model, scaler

def train_models(data_loader, lstm_params={}, transformer_params={}, grid_params={}, window_size=252, prediction_days=5):
    models = {}
    lstm_models = {}
    arima_models = {}
    garch_models = {}
    transformer_models = {}
    ml_models = {}

    for symbol, symbol_data in tqdm(data_loader, desc="Training Models"):
        try:
            logger.info(f"Processing data for {symbol}")
            
            # Ensure 'date' is in the index
            if 'date' in symbol_data.columns:
                symbol_data.set_index('date', inplace=True)
            
            X = symbol_data.drop(columns=['close', 'symbol'])
            y = symbol_data['close']
            
            # Convert index to DatetimeIndex if it's not already
            if not isinstance(y.index, pd.DatetimeIndex):
                y.index = pd.to_datetime(y.index)
            
            y.index.freq = pd.infer_freq(y.index)
            
            logger.info(f"Training ensemble model for {symbol}")
            models[symbol] = train_ensemble_model(X, y, grid_params)
            
            logger.info(f"Training LSTM model for {symbol}")
            X_lstm = X.values.reshape((X.shape[0], 1, X.shape[1]))
            lstm_models[symbol] = train_lstm_model(X_lstm, y.values, X.shape[1], **lstm_params)
            
            logger.info(f"Training ARIMA model for {symbol}")
            arima_models[symbol] = ARIMA(y, order=(5, 1, 0)).fit()
            
            logger.info(f"Training GARCH model for {symbol}")
            scaled_y = y / 100  # Rescale the data
            garch_models[symbol] = arch_model(scaled_y, vol='Garch', p=1, q=1).fit(disp='off')
            
            logger.info(f"Training Transformer model for {symbol}")
            transformer_models[symbol] = train_transformer_model(X_lstm, y.values, X.shape[1], **transformer_params)
            
            logger.info(f"Training ML model for {symbol}")
            ml_models[symbol] = train_ml_model(X, y)
            
            logger.info(f"Successfully trained all models for {symbol}")
        except Exception as e:
            logger.error(f"Error training models for {symbol}: {str(e)}")

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