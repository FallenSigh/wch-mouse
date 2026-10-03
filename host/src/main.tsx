import React from "react";
import ReactDOM from "react-dom/client";

import App from "./App";
import { BioProvider } from "./BioContext";
import "./styles.css";

ReactDOM.createRoot(document.getElementById("root") as HTMLElement).render(
  <React.StrictMode>
    <BioProvider>
      <App />
    </BioProvider>
  </React.StrictMode>
);
