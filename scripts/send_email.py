#!/usr/bin/env python3
"""Send an email via Gmail SMTP. Used by datagen_harness for progress notifications.

Requires GMAIL_APP_PASSWORD environment variable (not your regular password).
To create one: https://myaccount.google.com/apppasswords

Usage:
    python3 scripts/send_email.py --to user@gmail.com --subject "test" --body "hello"
"""

import argparse
import os
import smtplib
import sys
from email.mime.text import MIMEText


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--to", required=True)
    parser.add_argument("--from-addr", default=None,
                        help="Sender address (defaults to --to)")
    parser.add_argument("--subject", required=True)
    parser.add_argument("--body", required=True)
    args = parser.parse_args()

    password = os.environ.get("GMAIL_APP_PASSWORD")
    if not password:
        print("GMAIL_APP_PASSWORD not set — skipping email", file=sys.stderr)
        sys.exit(1)

    sender = args.from_addr or args.to
    msg = MIMEText(args.body)
    msg["Subject"] = args.subject
    msg["From"] = sender
    msg["To"] = args.to

    try:
        with smtplib.SMTP_SSL("smtp.gmail.com", 465) as server:
            server.login(sender, password)
            server.sendmail(sender, [args.to], msg.as_string())
        print(f"Email sent to {args.to}", file=sys.stderr)
    except Exception as e:
        print(f"Email failed: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
