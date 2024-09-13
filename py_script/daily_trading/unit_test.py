import unittest
import pandas as pd
import numpy as np
import torch
import torch.nn as nn
from unittest.mock import patch, MagicMock
from data.data_preprocessing import preprocess_data, preprocess_symbol_data
from data.data_fetching import fetch_data_from_db, fetch_data_from_memory
from models.model_training import train_lstm_model, train_transformer_model, LSTMModel, TransformerModel
from strategies.combined_strategy import bollinger_bands, macd, rsi, generate_trading_signals
from strategies.risk_management import apply_risk_management
from trading.backtesting import calculate_metrics, backtest
from data.data_loader import AsyncDataLoader
from config.db_config import read_db_config
from sqlalchemy import create_engine

class TestDataPreprocessing(unittest.TestCase):
    def setUp(self):
        self.sample_data = pd.DataFrame({
            'date': pd.date_range(start='2023-01-01', periods=5),
            'symbol': ['AAPL'] * 5,
            'close': [100, 101, 99, 102, 103],
            'volume': [1000, 1100, 900, 1200, 1300]
        })

    def test_preprocess_data(self):
        processed_data = preprocess_data(self.sample_data)
        self.assertEqual(processed_data.index.name, 'date')
        self.assertFalse(processed_data.isnull().any().any())

    def test_preprocess_symbol_data(self):
        symbol_data = preprocess_symbol_data(self.sample_data, 'AAPL')
        self.assertEqual(symbol_data.index.name, 'date')
        self.assertEqual(len(symbol_data), 5)
        self.assertEqual(symbol_data['symbol'].unique()[0], 'AAPL')

class TestDataFetching(unittest.TestCase):
    def setUp(self):
        self.db_config = read_db_config()
        self.symbols = ['AAPL', 'GOOGL']

    @patch('data.data_fetching.create_engine')
    @patch('data.data_fetching.fetch_data_from_db')
    def test_fetch_data_from_db(self, mock_fetch_data_from_db, mock_create_engine):
        mock_engine = MagicMock()
        mock_create_engine.return_value = mock_engine
        mock_connection = mock_engine.connect.return_value
        mock_fetch_data_from_db.return_value = iter([('AAPL', pd.DataFrame({'date': pd.date_range(start='2023-01-01', periods=5), 'symbol': ['AAPL']*5, 'close': [100, 101, 99, 102, 103], 'volume': [1000, 1100, 900, 1200, 1300]})), ('GOOGL', pd.DataFrame({'date': pd.date_range(start='2023-01-01', periods=5), 'symbol': ['GOOGL']*5, 'close': [100, 101, 99, 102, 103], 'volume': [1000, 1100, 900, 1200, 1300]}))])
        
        result = list(fetch_data_from_db(self.symbols, self.db_config, '2023-01-01', '2023-12-31'))
        mock_connection.close()
        mock_create_engine.assert_called_once()
        mock_connection.close.assert_called_once()
        self.assertEqual(len(result), 2)
        self.assertIsInstance(result[0][1], pd.DataFrame)  # Check the DataFrame in the tuple
        self.assertIsInstance(result[1][1], pd.DataFrame)  # Check the DataFrame in the tuple

    def test_fetch_data_from_memory(self):
        sample_data = pd.DataFrame({
            'date': pd.date_range(start='2023-01-01', periods=10),
            'symbol': ['AAPL'] * 5 + ['GOOGL'] * 5,
            'close': [100, 101, 99, 102, 103] * 2,
            'volume': [1000, 1100, 900, 1200, 1300] * 2
        })
        result = list(fetch_data_from_memory(sample_data))
        self.assertEqual(len(result), 2)
        self.assertIsInstance(result[0], pd.DataFrame)
        self.assertIsInstance(result[1], pd.DataFrame)

class TestModelTraining(unittest.TestCase):
    def setUp(self):
        self.batch_size = 32
        self.seq_length = 10
        self.input_size = 5
        self.hidden_size = 64
        self.num_layers = 2
        self.device = torch.device("mps") if torch.backends.mps.is_available() else torch.device("cuda" if torch.cuda.is_available() else "cpu")
        self.X_train = np.random.randn(self.batch_size, self.seq_length, self.input_size)
        self.y_train = np.random.randn(self.batch_size)

    def test_lstm_model(self):
        model = LSTMModel(input_size=self.input_size, hidden_size=self.hidden_size, num_layers=self.num_layers)
        self.assertIsInstance(model, nn.Module)
        
        input_tensor = torch.randn(self.batch_size, self.seq_length, self.input_size)
        output = model(input_tensor)
        self.assertEqual(output.shape, (self.batch_size,))

    def test_transformer_model(self):
        model = TransformerModel(input_size=6, num_heads=2, num_layers=self.num_layers)  # Ensure input_size is divisible by num_heads
        self.assertIsInstance(model, nn.Module)
        
        input_tensor = torch.randn(self.batch_size, self.seq_length, 6)  # Adjust input_size
        output = model(input_tensor)
        self.assertEqual(output.shape, (self.batch_size,))

    @patch('torch.nn.LSTM')
    @patch('torch.optim.Adam')
    @patch('torch.optim.lr_scheduler.StepLR')
    def test_train_lstm_model(self, mock_scheduler, mock_adam, mock_lstm):
        mock_lstm_instance = mock_lstm.return_value
        mock_lstm_instance.return_value = (torch.randn(self.batch_size, self.seq_length, self.hidden_size), 
                                           (torch.randn(self.num_layers, self.batch_size, self.hidden_size), 
                                            torch.randn(self.num_layers, self.batch_size, self.hidden_size)))
        with patch('models.model_training.device', self.device):
            model = train_lstm_model(self.X_train, self.y_train, input_size=self.input_size, hidden_size=self.hidden_size, epochs=1)
        self.assertIsNotNone(model)
        mock_lstm.assert_called_once()
        mock_adam.assert_called_once()
        mock_scheduler.assert_called_once()

    @patch('torch.nn.Transformer')
    @patch('torch.optim.Adam')
    @patch('torch.optim.lr_scheduler.StepLR')
    def test_train_transformer_model(self, mock_scheduler, mock_adam, mock_transformer):
        mock_transformer_instance = mock_transformer.return_value
        mock_transformer_instance.return_value = torch.randn(self.seq_length, self.batch_size, self.input_size)
        with patch('models.model_training.device', self.device):
            model = train_transformer_model(self.X_train, self.y_train, input_size=self.input_size, num_heads=2, num_layers=2, epochs=1)
        self.assertIsNotNone(model)
        mock_transformer.assert_called_once()
        mock_adam.assert_called_once()
        mock_scheduler.assert_called_once()

    def test_device_selection(self):
        self.assertIn(self.device.type, ['cuda', 'cpu', 'mps'])

class TestCombinedStrategy(unittest.TestCase):
    def setUp(self):
        self.data = pd.DataFrame({
            'close': np.random.rand(100) * 100,
            'volume': np.random.randint(1000, 10000, 100)
        })

    def test_bollinger_bands(self):
        upper_band, lower_band = bollinger_bands(self.data)
        self.assertEqual(len(upper_band), 100)
        self.assertEqual(len(lower_band), 100)
        self.assertTrue((upper_band >= lower_band).mean() > 0.8)

    def test_macd(self):
        macd_line, signal_line = macd(self.data)
        self.assertEqual(len(macd_line), 100)
        self.assertEqual(len(signal_line), 100)

    def test_rsi(self):
        rsi_values = rsi(self.data)
        self.assertEqual(len(rsi_values), 100)
        self.assertTrue(all(0 <= value <= 100 for value in rsi_values.dropna()))

    def test_generate_trading_signals(self):
        predictions = np.random.rand(100)
        actual_prices = self.data['close'].values
        ml_model = MagicMock()
        ml_model.predict_proba.return_value = np.random.rand(100, 2)
        scaler = MagicMock()
        scaler.transform.return_value = np.random.rand(100, 10)
        
        signals = generate_trading_signals(predictions, actual_prices, self.data, ml_model=ml_model, scaler=scaler)
        self.assertEqual(len(signals), 100)
        self.assertTrue(all(signal in ["buy", "sell", "hold"] for signal in signals))

class TestRiskManagement(unittest.TestCase):
    def setUp(self):
        self.predictions = [100, 200, 150, 300, 250]
        self.data = pd.DataFrame({
            'symbol': ['AAPL', 'GOOGL', 'MSFT', 'AMZN', 'FB'],
            'close': [150, 2500, 300, 3000, 300],
            'returns': [0.01, 0.02, -0.01, 0.03, -0.02]
        })

    def test_apply_risk_management(self):
        risk_adjusted_predictions = apply_risk_management(self.predictions, self.data)
        self.assertEqual(len(risk_adjusted_predictions), len(self.predictions))
        self.assertTrue(all(0 <= pred <= 10000 for pred in risk_adjusted_predictions))

class TestBacktesting(unittest.TestCase):
    def setUp(self):
        self.db_config = read_db_config()
        self.symbols = ['AAPL']
        
        # Mock data loader
        self.data_loader = MagicMock()
        self.data_loader.__iter__.return_value = iter([('AAPL', pd.DataFrame({
            'date': pd.date_range(start='2023-01-01', periods=100),
            'close': np.random.rand(100) * 100,
            'symbol': ['AAPL'] * 100
        }))])

    def test_calculate_metrics(self):
        df = pd.DataFrame({
            'actual': np.random.rand(100) * 100,
            'predicted': np.random.rand(100) * 100
        })
        metrics = calculate_metrics(df)
        self.assertIn('cumulative_returns', metrics)
        self.assertIn('cumulative_strategy_returns', metrics)
        self.assertIn('sharpe_ratio', metrics)
        self.assertIn('max_drawdown', metrics)
        self.assertIn('mean_squared_error', metrics)
        self.assertIn('r2_score', metrics)

    @patch('trading.backtesting.make_predictions')
    def test_backtest(self, mock_make_predictions):
        mock_make_predictions.return_value = np.random.rand(5)
        
        models = {'AAPL': MagicMock()}
        lstm_models = {'AAPL': MagicMock()}
        arima_models = {'AAPL': MagicMock()}
        garch_models = {'AAPL': MagicMock()}
        transformer_models = {'AAPL': MagicMock()}
        ml_models = {'AAPL': (MagicMock(), MagicMock())}
        
        results, best_model = backtest(self.data_loader, models, lstm_models, arima_models, garch_models, transformer_models, ml_models, window_size=50, prediction_days=5)
        
        self.assertIsInstance(results, dict)
        self.assertIn('AAPL', results)
        self.assertIsInstance(best_model, str)

class TestAsyncDataLoader(unittest.TestCase):
    @patch('data.data_loader.fetch_data_from_db')
    def test_async_data_loader(self, mock_fetch_data_from_db):
        symbols = ['AAPL']
        db_config = read_db_config()
        mock_fetch_data_from_db.return_value = iter([('AAPL', pd.DataFrame({
            'date': pd.date_range(start='2023-01-01', periods=100),
            'close': np.random.rand(100) * 100,
            'symbol': ['AAPL'] * 100
        }))])
        
        loader = AsyncDataLoader(symbols, db_config)
        loader.start()

        data = next(iter(loader))
        self.assertIsInstance(data, tuple)
        self.assertEqual(data[0], 'AAPL')
        self.assertIsInstance(data[1], pd.DataFrame)

        loader.stop()

if __name__ == '__main__':
    unittest.main()