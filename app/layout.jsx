import './globals.css';

export const metadata = {
  title: 'Canda Bot | ESP32 Monitor',
  description: 'Live ESP32 hardware and network monitoring dashboard'
};

export default function RootLayout({ children }) {
  return (
    <html lang="en">
      <body>{children}</body>
    </html>
  );
}
