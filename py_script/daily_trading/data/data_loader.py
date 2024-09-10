import threading
import queue
import time
from data.data_fetching import fetch_data_from_db, fetch_data_from_memory
from data.data_preprocessing import preprocess_data, preprocess_symbol_data

class AsyncDataLoader:
    def __init__(self, symbols, db_config, load_from_memory=False, data=None, batch_size=1024, num_workers=8, max_queue_size=5):
        self.symbols = symbols
        self.db_config = db_config
        self.load_from_memory = load_from_memory
        self.data = data
        self.batch_size = batch_size
        self.num_workers = num_workers
        self.max_queue_size = max_queue_size
        self.queue = queue.Queue(maxsize=max_queue_size)
        self.stop_event = threading.Event()
        self.workers = []

    def start(self):
        for _ in range(self.num_workers):
            worker = threading.Thread(target=self._worker_fn)
            worker.start()
            self.workers.append(worker)

    def stop(self):
        self.stop_event.set()
        for worker in self.workers:
            worker.join()

    def _worker_fn(self):
        while not self.stop_event.is_set():
            try:
                if self.load_from_memory:
                    data_generator = fetch_data_from_memory(self.data)
                else:
                    data_generator = fetch_data_from_db(self.symbols, self.db_config)
                
                for chunk in data_generator:
                    processed_chunk = preprocess_data(chunk)
                    for symbol in self.symbols:
                        symbol_data = preprocess_symbol_data(processed_chunk, symbol)
                        self.queue.put((symbol, symbol_data), block=True, timeout=1)
                    if self.stop_event.is_set():
                        break
            except queue.Full:
                time.sleep(0.1)

    def __iter__(self):
        return self

    def __next__(self):
        if self.stop_event.is_set() and self.queue.empty():
            raise StopIteration
        try:
            return self.queue.get(block=True, timeout=1)
        except queue.Empty:
            if self.stop_event.is_set():
                raise StopIteration
            return self.__next__()